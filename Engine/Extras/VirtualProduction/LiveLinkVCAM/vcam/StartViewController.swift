//
//  StartViewController.swift
//  vcam
//
//  Created by Brian Smith on 11/10/20.
//  Copyright Epic Games, Inc. All Rights Reserved.
//

import UIKit
import CocoaAsyncSocket
import Kronos
import GameController

class StartViewController : BaseViewController {
    
    static var DefaultPort = UInt16(2049)

    @IBOutlet weak var headerView : HeaderView!
    @IBOutlet weak var versionLabel : UILabel!

    @IBOutlet weak var restartView : UIView!

    @IBOutlet weak var entryView : UIView!
    @IBOutlet weak var entryViewYConstraint : NSLayoutConstraint!

    @IBOutlet weak var ipAddress : UITextField!
    @IBOutlet weak var connect : UIButton!

    @IBOutlet weak var connectingView : UIVisualEffectView!

    private var tapGesture : UITapGestureRecognizer!
    private var oscConnection : OSCTCPConnection?
    
    public var multicastWatchdogSocket  : GCDAsyncUdpSocket?
    
    private var liveLink : LiveLinkProvider?
    private var liveLinkTimer : Timer?
    
    @objc dynamic let appSettings = AppSettings.shared
    private var observers = [NSKeyValueObservation]()
    
    private var gameController : GCController?

    var ipAddressIsDemoMode : Bool {
        self.ipAddress.text == "demo.mode"
    }
    
    override var preferredStatusBarStyle: UIStatusBarStyle {
          return .lightContent
    }
    
    override func viewDidLoad() {
        
        super.viewDidLoad();
        
        self.restartView.isHidden = true
        
        if let infoDict = Bundle.main.infoDictionary {
            self.versionLabel.text = String(format: "v%@ (%@)", infoDict["CFBundleShortVersionString"] as! String, infoDict["CFBundleVersion"] as! String)
        } else {
            self.versionLabel.text = "";
        }
        
        NetUtility.triggerLocalNetworkPrivacyAlert()
        
        self.ipAddress.text = AppSettings.shared.lastConnectionAddress
        textFieldChanged(self.ipAddress)
        
        self.tapGesture = UITapGestureRecognizer(target: self, action: #selector(handleTap))
        self.tapGesture.cancelsTouchesInView = false
        self.tapGesture.delegate = self
        self.view.addGestureRecognizer(tapGesture)
        
        NotificationCenter.default.addObserver(self, selector: #selector(keyboardWillShow), name: UIResponder.keyboardWillShowNotification, object: nil)
        NotificationCenter.default.addObserver(self, selector: #selector(keyboardWillHide), name: UIResponder.keyboardWillHideNotification, object: nil)
        
        observers.append(observe(\.appSettings.timecodeSource, options: [.initial,.new,.old], changeHandler: { object, change in
            
            if let oldValue = TimecodeSource(rawValue:change.oldValue ?? 0) {
                switch oldValue {
                case .ntp:
                    Clock.reset()
                case .tentacleSync:
                    Tentacle.shared = nil
                default:
                    break
                }
            }
            
            switch self.appSettings.timecodeSourceEnum() {
            case .ntp:
                let pool = AppSettings.shared.ntpPool.isEmpty ? "time.apple.com" : AppSettings.shared.ntpPool
                Log.info("Started NTP : \(pool)")
                
                // IMPORTANT
                // We reset the NTP clock first --
                // otherwise we don't know if the NTP address that is used for the pool is valid or not because it
                // will be using a stale last valid time
                Clock.reset()
                Clock.sync(from: pool)
            case .tentacleSync:
                Tentacle.shared = Tentacle()
            default:
                break
            }
            
        }))
        
        // any change to the subject name will remove & re-add the camera subject.
        observers.append(observe(\.appSettings.liveLinkSubjectName, options: [.old,.new], changeHandler: { object, change in
            self.liveLink?.removeCameraSubject(change.oldValue!)
            self.liveLink?.addCameraSubject(self.appSettings.liveLinkSubjectName)
        }))
        
        NotificationCenter.default.addObserver(self, selector: #selector(gameControllerDidConnectNotification), name: .GCControllerDidConnect, object: nil)
        NotificationCenter.default.addObserver(self, selector: #selector(gameControllerDidDisconnectNotification), name: .GCControllerDidDisconnect, object: nil)
        
        self.gameController = GCController.controllers().first
        if let gc = self.gameController {
            if gc.isAttachedToDevice {
                gc.playerIndex = .index1
            }
        }
        
        LiveLink.initialize(self)
        restartLiveLink()
    }
     
    func restartLiveLink() {

        // stop the provider & restart livelink here
        if self.liveLink != nil {
            Log.info("Restarting Messaging Engine.")
            LiveLink.restart()
            self.liveLink = nil
        }

        Log.info("Initializing LiveLink Provider.")

        self.liveLink = LiveLink.createProvider("Live Link VCAM")
        self.liveLink?.addCameraSubject(AppSettings.shared.liveLinkSubjectName)

        if let videoViewController = self.presentedViewController as? VideoViewController {
            if !self.ipAddressIsDemoMode {
                videoViewController.liveLink = self.liveLink
            }
        }

        multicastWatchdogSocket?.close()
        Log.info("Starting multicast watchdog.")
        multicastWatchdogSocket = GCDAsyncUdpSocket(delegate: self, delegateQueue: DispatchQueue.main)
        do {
            try multicastWatchdogSocket?.enableReusePort(true)
            try multicastWatchdogSocket?.bind(toPort: 6665)
            try multicastWatchdogSocket?.joinMulticastGroup("230.0.0.1")
            try multicastWatchdogSocket?.beginReceiving()
        } catch {
            Log.info("Error creating watchdog : \(error.localizedDescription)")
        }
    }
    
    override func viewWillAppear(_ animated : Bool) {
        
        self.connectingView.isHidden = true
        self.headerView.start()
        
        liveLinkTimer?.invalidate()
        liveLinkTimer = Timer.scheduledTimer(withTimeInterval: 1.0/10.0, repeats: true, block: { timer in
            self.liveLink?.updateSubject(AppSettings.shared.liveLinkSubjectName, withTransform: simd_float4x4(), atTime: Timecode.create().toTimeInterval())
        })
    }
    
    override func viewDidAppear(_ animated: Bool) {
        super.viewDidAppear(animated)
    }
    
    override func viewDidDisappear(_ animated: Bool) {
        super.viewDidDisappear(animated)
        self.headerView.stop()
    }
    
    override func prepare(for segue: UIStoryboardSegue, sender: Any?) {

        // hide the keyboard if it was being shown
        self.view.endEditing(true)

        if segue.identifier == "showVideoView" {

            if let vc = segue.destination as? VideoViewController {
                
                // stop the timer locally which is sending LL identity xform
                liveLinkTimer?.invalidate()
                liveLinkTimer = nil

                self.oscConnection?.delegate = vc
                vc.oscConnection = self.oscConnection
                
                vc.liveLink = self.liveLink
                vc.gameController = self.gameController

                self.oscConnection = nil
            }
        }
    }
    
    @objc func keyboardWillShow(notification: NSNotification) {
        
        guard let userInfo = notification.userInfo else {return}

        // if the keyboard will overlap the connect button, then we move the view up so that nothing
        // is obscured. In cases where there is a keyboard connected, the toolbar is shown only and
        // nothing will move.
        
        let keyboardFrame = self.view.convert((userInfo[UIResponder.keyboardFrameEndUserInfoKey] as! NSValue).cgRectValue, from: self.view.window)
        let connectButtonFrame = self.view.convert(connect.frame, from: connect.superview)
        
        if keyboardFrame.minY < connectButtonFrame.maxY {
            self.entryViewYConstraint.constant = -(self.view.frame.height - keyboardFrame.size.height) / 2.0
        } else {
            self.entryViewYConstraint.constant = 0
        }

        UIView.animate(withDuration: 0.2) {
            self.view.layoutIfNeeded()
        }
    }

    @objc func keyboardWillHide(notification: NSNotification) {
        if self.entryViewYConstraint.constant != 0 {
            self.entryViewYConstraint.constant = 0

            UIView.animate(withDuration: 0.2) {
                self.view.layoutIfNeeded()
            }
        }
    }
    
    @objc func handleTap(_ recognizer: UITapGestureRecognizer) {
        
        self.view.endEditing(true)
    }
    
    @IBAction func connect(_ sender : Any?) {
        
        AppSettings.shared.lastConnectionAddress = self.ipAddress.text!

        if self.ipAddressIsDemoMode {
            
            self.performSegue(withIdentifier: "showVideoViewDemoMode", sender: self)

        } else {

            self.connectingView.isHidden = false
            self.connectingView.alpha = 0.0
            UIView.animate(withDuration: 0.2) {
                self.connectingView.alpha = 1.0
            }
            
            showConnectingAlertView(mode: .connecting) {
                self.oscConnection = nil
                self.hideConnectingView() { }
            }

            let (host,port) = NetUtility.hostAndPortFromAddress(self.ipAddress.text!)
            
            do {
                self.oscConnection = nil
                self.oscConnection = try OSCTCPConnection(host:host, port:port ?? StartViewController.DefaultPort, delegate: self)
                
            } catch {
                
                hideConnectingAlertView() {

                    let errorAlert = UIAlertController(title: Localized.titleError(), message: "Couldn't connect : \(error.localizedDescription)", preferredStyle: .alert)
                    errorAlert.addAction(UIAlertAction(title: Localized.buttonOK(), style: .default, handler: { _ in
                        self.hideConnectingView { }
                    }))
                    self.present(errorAlert, animated: true)
                }
            }
        }
    }
    
    func hideConnectingView( _ completion : @escaping () -> Void) {
        UIView.animate(withDuration: 0.2, animations: {
            self.connectingView.alpha = 0.0
        }, completion: { b in
            self.connectingView.isHidden = true
        })
        hideConnectingAlertView(completion)
    }
    
    @IBAction func textFieldChanged(_ sender : Any?) {
        self.connect.isEnabled = !self.ipAddress.text!.isEmpty
    }
    
    @objc func gameControllerDidConnectNotification(_ notification: NSNotification) {
        
        self.gameController = notification.object as? GCController
        self.gameController?.playerIndex = .index1
        
        if let videoViewController = self.presentedViewController as? VideoViewController {
            videoViewController.gameController = self.gameController
        }
    }

    @objc func gameControllerDidDisconnectNotification(_ notification: NSNotification) {
        if let gc = notification.object as? GCController {
            Log.info("gameControllerDidDisconnectNotification \(gc.vendorName ?? "Unknown controller")")
        }
    }

}

extension StartViewController : UIGestureRecognizerDelegate {
    
    func gestureRecognizer(_ gestureRecognizer: UIGestureRecognizer, shouldReceive touch: UITouch) -> Bool {
        
        return touch.view != self.connect && touch.view != self.ipAddress
    }
}


extension StartViewController : UITextFieldDelegate {
    
    func textFieldShouldReturn(_ textField: UITextField) -> Bool {
        textField.resignFirstResponder()
        
        connect(textField)
        
        return true
    }
}

extension StartViewController : GCDAsyncUdpSocketDelegate {

    func udpSocketDidClose(_ sock: GCDAsyncUdpSocket, withError error: Error?) {
        Log.error("Multicast watchdog closed : restarting LiveLink.")
        restartLiveLink()
    }
}

extension StartViewController : LiveLinkLogDelegate {

    func logMessage(_ message: String!) {
        Log.info(message)
    }
}
