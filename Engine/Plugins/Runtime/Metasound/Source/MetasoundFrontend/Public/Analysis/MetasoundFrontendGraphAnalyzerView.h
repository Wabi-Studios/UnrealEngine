// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Analysis/MetasoundFrontendAnalyzerView.h"
#include "Analysis/MetasoundFrontendGraphAnalyzer.h"
#include "Containers/Map.h"
#include "HAL/Platform.h"
#include "MetasoundAssetBase.h"
#include "MetasoundRouter.h"
#include "Templates/UniquePtr.h"


// Forward Declarations
class FMetasoundAssetBase;
class UAudioComponent;

namespace Metasound
{
	namespace Frontend
	{
		class METASOUNDFRONTEND_API FMetasoundGraphAnalyzerView
		{
			// Sender in charge of supplying expected vertex analyzers currently being analyzed.
			TUniquePtr<ISender> ActiveAnalyzerSender;

			// Set of active keys used for what senders should be active on the instance graph
			TSet<FString> ActiveAnalyzerKeys;

			const FMetasoundAssetBase& GetMetaSoundAssetChecked() const;

			uint64 InstanceID = INDEX_NONE;
			const FMetasoundAssetBase* MetaSoundAsset = nullptr;

			FOperatorSettings OperatorSettings = { 48000, 100 };

			using FMetasoundGraphAnalyzerOutputKey = TTuple<FGuid /* NodeID */, FName /* OutputName */>;
			TMap<FMetasoundGraphAnalyzerOutputKey, TArray<FMetasoundAnalyzerView>> AnalyzerViews;

		public:
			FMetasoundGraphAnalyzerView() = default;
			FMetasoundGraphAnalyzerView(const FMetasoundAssetBase& InAssetBase, uint64 InInstanceID, FSampleRate InSampleRate);

			void AddAnalyzerForAllSupportedOutputs(FName InAnalyzerName, bool bInRequiresConnection = true);
			void RemoveAnalyzerForAllSupportedOutputs(FName InAnalyzerName);
			TArray<FMetasoundAnalyzerView*> GetAnalyzerViews(FName InAnalyzerName);
			TArray<const FMetasoundAnalyzerView*> GetAnalyzerViews(FName InAnalyzerName) const;
			TArray<FMetasoundAnalyzerView*> GetAnalyzerViewsForOutput(const FGuid& InNodeID, FName InOutputName, FName InAnalyzerName);
			TArray<const FMetasoundAnalyzerView*> GetAnalyzerViewsForOutput(const FGuid& InNodeID, FName InOutputName, FName InAnalyzerName) const;
		};
	}
}
