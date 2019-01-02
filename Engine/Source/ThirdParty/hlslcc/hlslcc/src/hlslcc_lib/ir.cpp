// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

// This code is modified from that in the Mesa3D Graphics library available at
// http://mesa3d.org/
// The license for the original code follows:

/*
* Copyright © 2010 Intel Corporation
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice (including the next
* paragraph) shall be included in all copies or substantial portions of the
* Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/
#include "ShaderCompilerCommon.h"
#include "ir.h"
#include "ir_visitor.h"
#include "glsl_types.h"
#include "macros.h"

int ir_instruction::ID = 0;
int ir_instruction::BREAK_ON_ID = -1;

ir_rvalue::ir_rvalue()
{
	this->type = glsl_type::error_type;
}

bool ir_rvalue::is_zero() const
{
	return false;
}

bool ir_rvalue::is_one() const
{
	return false;
}

bool ir_rvalue::is_negative_one() const
{
	return false;
}

/**
* Modify the swizzle make to move one component to another
*
* \param m    IR swizzle to be modified
* \param from Component in the RHS that is to be swizzled
* \param to   Desired swizzle location of \c from
*/
static void update_rhs_swizzle(ir_swizzle_mask &m, unsigned from, unsigned to)
{
	switch (to)
	{
	case 0: m.x = from; break;
	case 1: m.y = from; break;
	case 2: m.z = from; break;
	case 3: m.w = from; break;
	default: check(!"Should not get here.");
	}

	m.num_components = MAX2(m.num_components, (to + 1));
}

void ir_assignment::set_lhs(ir_rvalue* lhs)
{
	void *mem_ctx = this;
	ir_swizzle *swiz = NULL;
	bool swizzled = false;

	while (lhs != NULL)
	{
		swiz = lhs->as_swizzle();

		if (swiz == NULL || swiz->val->type->is_matrix())
			break;

		check(swiz->val->type->is_matrix() == false);

		unsigned write_mask = 0;
		ir_swizzle_mask rhs_swiz = { 0, 0, 0, 0, 0, 0 };

		for (unsigned i = 0; i < swiz->mask.num_components; i++)
		{
			unsigned c = 0;

			switch (i)
			{
			case 0: c = swiz->mask.x; break;
			case 1: c = swiz->mask.y; break;
			case 2: c = swiz->mask.z; break;
			case 3: c = swiz->mask.w; break;
			default: check(!"Should not get here.");
			}

			write_mask |= (((this->write_mask >> i) & 1) << c);
			update_rhs_swizzle(rhs_swiz, i, c);
		}

		this->write_mask = write_mask;
		lhs = swiz->val;

		this->rhs = new(mem_ctx)ir_swizzle(this->rhs, rhs_swiz);
		swizzled = true;
	}

	if (swizzled)
	{
		/* Now, RHS channels line up with the LHS writemask.  Collapse it
		* to just the channels that will be written.
		*/
		ir_swizzle_mask rhs_swiz = { 0, 0, 0, 0, 0, 0 };
		int rhs_chan = 0;
		for (int i = 0; i < 4; i++)
		{
			if (write_mask & (1 << i))
				update_rhs_swizzle(rhs_swiz, i, rhs_chan++);
		}
		this->rhs = new(mem_ctx)ir_swizzle(this->rhs, rhs_swiz);
	}

	if (swiz)
	{
		// If a swizzle hasn't been resolved, it must be a matrix swizzle.
		check(swiz->val->type->is_matrix());

		unsigned write_mask = 0;
		for (unsigned i = 0; i < swiz->mask.num_components; i++)
		{
			unsigned c = 0;
			switch (i)
			{
			case 0: c = swiz->mask.x; break;
			case 1: c = swiz->mask.y; break;
			case 2: c = swiz->mask.z; break;
			case 3: c = swiz->mask.w; break;
			default: check(!"Should not get here.");
			}

			write_mask |= (((this->write_mask >> i) & 1) << c);
		}
		this->write_mask = write_mask;
		lhs = swiz->val;
	}

	check((lhs == NULL) || lhs->as_dereference());

	this->lhs = (ir_dereference *)lhs;
}

ir_variable *
ir_assignment::whole_variable_written()
{
	ir_variable *v = this->lhs->whole_variable_referenced();

	if (v == NULL)
		return NULL;

	if (v->type->is_scalar())
		return v;

	if (v->type->is_vector())
	{
		const unsigned mask = (1U << v->type->vector_elements) - 1;

		if (mask != this->write_mask)
			return NULL;
	}

	/* Either all the vector components are assigned or the variable is some
	* composite type (and the whole thing is assigned.
	*/
	return v;
}

ir_assignment::ir_assignment(ir_dereference *lhs, ir_rvalue* rhs,
	ir_rvalue* condition, unsigned write_mask)
{
	this->ir_type = ir_type_assignment;
	this->condition = condition;
	this->rhs = rhs;
	this->lhs = lhs;
	this->write_mask = write_mask;

	if (lhs->type->is_scalar() || lhs->type->is_vector())
	{
		int lhs_components = 0;
		for (int i = 0; i < 4; i++)
		{
			if (write_mask & (1 << i))
				lhs_components++;
		}

		check(lhs_components == this->rhs->type->vector_elements);
	}
}

ir_assignment::ir_assignment(ir_rvalue* lhs, ir_rvalue* rhs,
	ir_rvalue* condition)
{
	this->ir_type = ir_type_assignment;
	this->condition = condition;
	this->rhs = rhs;

	/* If the RHS is a vector type, assume that all components of the vector
	* type are being written to the LHS.  The write mask comes from the RHS
	* because we can have a case where the LHS is a vec4 and the RHS is a
	* vec3.  In that case, the assignment is:
	*
	*     (assign (...) (xyz) (var_ref lhs) (var_ref rhs))
	*/
	if (rhs->type->is_vector())
		this->write_mask = (1U << rhs->type->vector_elements) - 1;
	else if (rhs->type->is_scalar())
		this->write_mask = 1;
	else
		this->write_mask = 0;

	this->set_lhs(lhs);
}

bool ir_assignment::IsEquivalent(ir_assignment* IR)
{
	if (write_mask != IR->write_mask)
	{
		return false;
	}

	return AreEquivalent(lhs, IR->lhs) && AreEquivalent(rhs, IR->rhs) && AreEquivalent(condition, IR->condition);
}


ir_expression::ir_expression(int op, const struct glsl_type *type,
	ir_rvalue* op0)
{
	check(get_num_operands(ir_expression_operation(op)) == 1);
	this->ir_type = ir_type_expression;
	this->type = type;
	this->operation = ir_expression_operation(op);
	this->operands[0] = op0;
	this->operands[1] = NULL;
	this->operands[2] = NULL;
	this->operands[3] = NULL;
}

ir_expression::ir_expression(int op, const struct glsl_type *type,
	ir_rvalue* op0, ir_rvalue* op1)
{
	check(((op1 == NULL) && (get_num_operands(ir_expression_operation(op)) == 1))
		|| (get_num_operands(ir_expression_operation(op)) == 2));
	this->ir_type = ir_type_expression;
	this->type = type;
	this->operation = ir_expression_operation(op);
	this->operands[0] = op0;
	this->operands[1] = op1;
	this->operands[2] = NULL;
	this->operands[3] = NULL;
}

ir_expression::ir_expression(int op, const struct glsl_type *type,
	ir_rvalue* op0, ir_rvalue* op1,
	ir_rvalue* op2, ir_rvalue* op3)
{
	this->ir_type = ir_type_expression;
	this->type = type;
	this->operation = ir_expression_operation(op);
	this->operands[0] = op0;
	this->operands[1] = op1;
	this->operands[2] = op2;
	this->operands[3] = op3;
}

ir_expression::ir_expression(int op, ir_rvalue* op0)
{
	this->ir_type = ir_type_expression;

	this->operation = ir_expression_operation(op);
	this->operands[0] = op0;
	this->operands[1] = NULL;
	this->operands[2] = NULL;
	this->operands[3] = NULL;

	check(op <= ir_last_unop);

	switch (this->operation)
	{
	case ir_unop_bit_not:
	case ir_unop_logic_not:
	case ir_unop_neg:
	case ir_unop_abs:
	case ir_unop_sign:
	case ir_unop_rcp:
	case ir_unop_rsq:
	case ir_unop_sqrt:
	case ir_unop_exp:
	case ir_unop_log:
	case ir_unop_exp2:
	case ir_unop_log2:
	case ir_unop_trunc:
	case ir_unop_ceil:
	case ir_unop_floor:
	case ir_unop_fract:
	case ir_unop_round:
	case ir_unop_sin:
	case ir_unop_cos:
	case ir_unop_tan:
	case ir_unop_asin:
	case ir_unop_acos:
	case ir_unop_atan:
	case ir_unop_sinh:
	case ir_unop_cosh:
	case ir_unop_tanh:
	case ir_unop_normalize:
	case ir_unop_dFdx:
	case ir_unop_dFdy:
	case ir_unop_bitreverse:
	case ir_unop_saturate:
		this->type = op0->type;
		break;

	case ir_unop_f2i:
	case ir_unop_b2i:
	case ir_unop_u2i:
	case ir_unop_h2i:
	case ir_unop_fasi:
	case ir_unop_bitcount:
	case ir_unop_msb:
	case ir_unop_lsb:
		this->type = glsl_type::get_instance(GLSL_TYPE_INT,
			op0->type->vector_elements, 1);
		break;

	case ir_unop_b2h:
	case ir_unop_i2h:
	case ir_unop_u2h:
	case ir_unop_f2h:
		this->type = glsl_type::get_instance(GLSL_TYPE_HALF,
			op0->type->vector_elements, 1);
		break;

	case ir_unop_b2f:
	case ir_unop_i2f:
	case ir_unop_u2f:
	case ir_unop_h2f:
	case ir_unop_iasf:
	case ir_unop_uasf:
		this->type = glsl_type::get_instance(GLSL_TYPE_FLOAT,
			op0->type->vector_elements, 1);
		break;

	case ir_unop_f2b:
	case ir_unop_h2b:
	case ir_unop_i2b:
	case ir_unop_u2b:
		this->type = glsl_type::get_instance(GLSL_TYPE_BOOL,
			op0->type->vector_elements, 1);
		break;

	case ir_unop_i2u:
	case ir_unop_f2u:
	case ir_unop_h2u:
	case ir_unop_b2u:
	case ir_unop_fasu:
		this->type = glsl_type::get_instance(GLSL_TYPE_UINT,
			op0->type->vector_elements, 1);
		break;

	case ir_unop_noise:
		this->type = glsl_type::float_type;
		break;

	case ir_unop_isnan:
	case ir_unop_isinf:
		this->type = glsl_type::get_instance(GLSL_TYPE_BOOL,
			op0->type->vector_elements, op0->type->matrix_columns);
		break;

	case ir_unop_any:
	case ir_unop_all:
		this->type = glsl_type::bool_type;
		break;

	default:
		check(!"not reached: missing automatic type setup for ir_expression");
		this->type = op0->type;
		break;
	}
}

ir_expression::ir_expression(int op, ir_rvalue* op0, ir_rvalue* op1)
{
	this->ir_type = ir_type_expression;

	this->operation = ir_expression_operation(op);
	this->operands[0] = op0;
	this->operands[1] = op1;
	this->operands[2] = NULL;
	this->operands[3] = NULL;

	check(op > ir_last_unop);

	switch (this->operation)
	{
	case ir_binop_all_equal:
	case ir_binop_any_nequal:
		this->type = glsl_type::bool_type;
		break;

	case ir_binop_add:
	case ir_binop_sub:
	case ir_binop_min:
	case ir_binop_max:
	case ir_binop_pow:
	case ir_binop_mul:
	case ir_binop_div:
	case ir_binop_mod:
		if (op0->type->is_scalar())
		{
			this->type = op1->type;
		}
		else if (op1->type->is_scalar())
		{
			this->type = op0->type;
		}
		else
		{
			check(op0->type == op1->type);
			this->type = op0->type;
		}
		break;

	case ir_binop_logic_and:
	case ir_binop_logic_xor:
	case ir_binop_logic_or:
	case ir_binop_bit_and:
	case ir_binop_bit_xor:
	case ir_binop_bit_or:
		if (op0->type->is_scalar())
		{
			this->type = op1->type;
		}
		else if (op1->type->is_scalar())
		{
			this->type = op0->type;
		}
		else
		{
			check(op0->type == op1->type);
			this->type = op0->type;
		}
		break;

	case ir_binop_equal:
	case ir_binop_nequal:
	case ir_binop_lequal:
	case ir_binop_gequal:
	case ir_binop_less:
	case ir_binop_greater:
		check(op0->type == op1->type);
		this->type = glsl_type::get_instance(GLSL_TYPE_BOOL,
			op0->type->vector_elements, 1);
		break;

	case ir_binop_dot:
		if (op0->type->base_type == GLSL_TYPE_HALF && op1->type->base_type == GLSL_TYPE_HALF)
		{
			this->type = glsl_type::half_type;
		}
		else
		{
			this->type = glsl_type::float_type;
		}
		break;

	case ir_binop_cross:
		this->type = glsl_type::vec3_type;
		break;

	case ir_binop_lshift:
	case ir_binop_rshift:
		this->type = op0->type;
		break;

	case ir_binop_step:
		this->type = op1->type;
		break;

	default:
		check(!"not reached: missing automatic type setup for ir_expression");
		this->type = glsl_type::float_type;
	}
}

unsigned int ir_expression::get_num_operands(ir_expression_operation op)
{
	check(op <= ir_last_opcode);

	if (op <= ir_last_unop)
	{
		return 1;
	}

	if (op <= ir_last_binop)
	{
		return 2;
	}

	if (op <= ir_last_ternop)
	{
		return 3;
	}

	if (op == ir_quadop_vector)
	{
		return 4;
	}

	check(false);
	return 0;
}

static const char *const operator_strs[ir_opcode_count] =
{
	"~",
	"!",
	"neg",
	"abs",
	"sign",
	"rcp",
	"rsq",
	"sqrt",
	"exp",
	"log",
	"exp2",
	"log2",

	"f2i",
	"i2f",
	"f2b",
	"b2f",
	"i2b",
	"b2i",
	"b2u",
	"u2b",
	"f2u",
	"u2f",
	"i2u",
	"u2i",
	"h2i",
	"i2h",
	"h2f",
	"f2h",
	"h2b",
	"b2h",
	"h2u",
	"u2h",

	"transpose",

	"any",
	"all",

	"trunc",
	"ceil",
	"floor",
	"fract",
	"round",

	"sin",
	"cos",
	"tan",
	"asin",
	"acos",
	"atan",
	"sinh",
	"cosh",
	"tanh",

	"normalize",

	"dFdx",
	"dFdy",
	"dFdxFine",
	"dFdyFine",
	"dFdxCoarse",
	"dFdyCoarse",

	"isnan",
	"isinf",

	"floatBitsToUint",
	"floatBitsToInt",
	"intBitsToFloat",
	"uintBitsToFloat",

	"bitfieldReverse",
	"bitCount",
	"findMSB",
	"findLSB",
	
	"saturate",

	"noise",

	"+",
	"-",
	"*",
	"/",

	"%",
	"modf",

	"step",

	"<",
	">",
	"<=",
	">=",
	"==",
	"!=",
	"all_equal",
	"any_nequal",

	"<<",
	">>",
	"&",
	"^",
	"|",

	"&&",
	"^^",
	"||",

	"dot",
	"cross",
	"min",
	"max",

	"atan2",

	"pow",

	"lerp",
	"smoothstep",
	"clamp",
	"fma",

	"vector",
};

static_assert(GetNumArrayElements(operator_strs) == (ir_quadop_vector + 1), "operator_strs_wrong_size");

const char *ir_expression::operator_string(ir_expression_operation op)
{
	check((unsigned int)op < GetNumArrayElements(operator_strs));
	check(GetNumArrayElements(operator_strs) == (ir_quadop_vector + 1));
	return operator_strs[op];
}

const char *ir_expression::operator_string()
{
	return operator_string(this->operation);
}

const char*
depth_layout_string(ir_depth_layout layout)
{
	switch (layout)
	{
	case ir_depth_layout_none:      return "";
	case ir_depth_layout_any:       return "depth_any";
	case ir_depth_layout_greater:   return "depth_greater";
	case ir_depth_layout_less:      return "depth_less";
	case ir_depth_layout_unchanged: return "depth_unchanged";

	default:
		check(0);
		return "";
	}
}

ir_expression_operation
ir_expression::get_operator(const char *str)
{
	const int operator_count = sizeof(operator_strs) / sizeof(operator_strs[0]);
	for (int op = 0; op < operator_count; op++)
	{
		if (strcmp(str, operator_strs[op]) == 0)
		{
			return (ir_expression_operation)op;
		}
	}
	return (ir_expression_operation)-1;
}

bool ir_expression::IsEquivalent(ir_expression* IR)
{
	if (operation != IR->operation)
	{
		return false;
	}

	for (unsigned i = 0; i < get_num_operands(); ++i)
	{
		if (!AreEquivalent(operands[i], IR->operands[i]))
		{
			return false;
		}
	}

	return true;
}

ir_constant::ir_constant()
{
	this->ir_type = ir_type_constant;
	this->type = glsl_type::error_type;
}

ir_constant::ir_constant(const struct glsl_type *type,
	const ir_constant_data *data)
{
	check((type->base_type >= GLSL_TYPE_UINT)
		&& (type->base_type <= GLSL_TYPE_BOOL));

	this->ir_type = ir_type_constant;
	this->type = type;
	memcpy(&this->value, data, sizeof(this->value));
}

ir_constant::ir_constant(float f)
{
	this->ir_type = ir_type_constant;
	this->type = glsl_type::float_type;
	this->value.f[0] = f;
	for (int i = 1; i < 16; i++) 
	{
		this->value.f[i] = 0;
	}
}

ir_constant::ir_constant(unsigned int u)
{
	this->ir_type = ir_type_constant;
	this->type = glsl_type::uint_type;
	this->value.u[0] = u;
	for (int i = 1; i < 16; i++)
	{
		this->value.u[i] = 0;
	}
}

ir_constant::ir_constant(int i)
{
	this->ir_type = ir_type_constant;
	this->type = glsl_type::int_type;
	this->value.i[0] = i;
	for (int i = 1; i < 16; i++)
	{
		this->value.i[i] = 0;
	}
}

ir_constant::ir_constant(bool b)
{
	this->ir_type = ir_type_constant;
	this->type = glsl_type::bool_type;
	this->value.b[0] = b;
	for (int i = 1; i < 16; i++)
	{
		this->value.b[i] = false;
	}
}

ir_constant::ir_constant(const ir_constant *c, unsigned i)
{
	this->ir_type = ir_type_constant;
	this->type = c->type->get_base_type();

	switch (this->type->base_type)
	{
	case GLSL_TYPE_UINT:  this->value.u[0] = c->value.u[i]; break;
	case GLSL_TYPE_INT:   this->value.i[0] = c->value.i[i]; break;
	case GLSL_TYPE_HALF:	this->value.f[0] = c->value.f[i]; break;
	case GLSL_TYPE_FLOAT: this->value.f[0] = c->value.f[i]; break;
	case GLSL_TYPE_BOOL:  this->value.b[0] = c->value.b[i]; break;
	default:              check(!"Should not get here."); break;
	}
}

ir_constant::ir_constant(const struct glsl_type *type, exec_list *value_list)
{
	this->ir_type = ir_type_constant;
	this->type = type;

	check(type->is_scalar() || type->is_vector() || type->is_matrix()
		|| type->is_record() || type->is_array());

	check(!type->is_array() || !type->fields.array->is_array());

	if (type->is_array())
	{
		this->array_elements = ralloc_array(this, ir_constant *, type->length);
		unsigned i = 0;
		foreach_list(node, value_list)
		{
			ir_constant *value = (ir_constant *)node;
			check(value->as_constant() != NULL);

			this->array_elements[i++] = value;
		}
		return;
	}

	/* If the constant is a record, the types of each of the entries in
	* value_list must be a 1-for-1 match with the structure components.  Each
	* entry must also be a constant.  Just move the nodes from the value_list
	* to the list in the ir_constant.
	*/
	/* FINISHME: Should there be some type checking and / or assertions here? */
	/* FINISHME: Should the new constant take ownership of the nodes from
	* FINISHME: value_list, or should it make copies?
	*/
	if (type->is_record())
	{
		value_list->move_nodes_to(&this->components);
		return;
	}

	for (unsigned i = 0; i < 16; i++)
	{
		this->value.u[i] = 0;
	}

	ir_constant *value = (ir_constant *)(value_list->head);

	/* Constructors with exactly one scalar argument are special for vectors
	* and matrices.  For vectors, the scalar value is replicated to fill all
	* the components.  For matrices, the scalar fills the components of the
	* diagonal while the rest is filled with 0.
	*/
	if (value->type->is_scalar() && value->next->is_tail_sentinel())
	{
		if (type->is_matrix())
		{
			/* Matrix - fill diagonal (rest is already set to 0) */
			check(type->is_float());
			for (unsigned i = 0; i < type->matrix_columns; i++)
			{
				this->value.f[i * type->vector_elements + i] = value->value.f[0];
			}
		}
		else
		{
			/* Vector or scalar - fill all components */
			switch (type->base_type)
			{
			case GLSL_TYPE_UINT:
			case GLSL_TYPE_INT:
				for (unsigned i = 0; i < type->components(); i++)
				{
					this->value.u[i] = value->value.u[0];
				}
				break;
			case GLSL_TYPE_HALF:
			case GLSL_TYPE_FLOAT:
				for (unsigned i = 0; i < type->components(); i++)
				{
					this->value.f[i] = value->value.f[0];
				}
				break;
			case GLSL_TYPE_BOOL:
				for (unsigned i = 0; i < type->components(); i++)
				{
					this->value.b[i] = value->value.b[0];
				}
				break;
			default:
				check(!"Should not get here.");
				break;
			}
		}
		return;
	}

	if (type->is_matrix() && value->type->is_matrix())
	{
		check(value->next->is_tail_sentinel());

		/* From section 5.4.2 of the GLSL 1.20 spec:
		* "If a matrix is constructed from a matrix, then each component
		*  (column i, row j) in the result that has a corresponding component
		*  (column i, row j) in the argument will be initialized from there."
		*/
		unsigned cols = MIN2(type->matrix_columns, value->type->matrix_columns);
		unsigned rows = MIN2(type->vector_elements, value->type->vector_elements);
		for (unsigned i = 0; i < cols; i++)
		{
			for (unsigned j = 0; j < rows; j++)
			{
				const unsigned src = i * value->type->vector_elements + j;
				const unsigned dst = i * type->vector_elements + j;
				this->value.f[dst] = value->value.f[src];
			}
		}

		/* "All other components will be initialized to the identity matrix." */
		for (unsigned i = cols; i < type->matrix_columns; i++)
		{
			this->value.f[i * type->vector_elements + i] = 1.0;
		}

		return;
	}

	/* Use each component from each entry in the value_list to initialize one
	* component of the constant being constructed.
	*/
	for (unsigned i = 0; i < type->components(); /* empty */)
	{
		check(value->as_constant() != NULL);
		check(!value->is_tail_sentinel());

		for (unsigned j = 0; j < value->type->components(); j++)
		{
			switch (type->base_type)
			{
			case GLSL_TYPE_UINT:
				this->value.u[i] = value->get_uint_component(j);
				break;
			case GLSL_TYPE_INT:
				this->value.i[i] = value->get_int_component(j);
				break;
			case GLSL_TYPE_HALF:
			case GLSL_TYPE_FLOAT:
				this->value.f[i] = value->get_float_component(j);
				break;
			case GLSL_TYPE_BOOL:
				this->value.b[i] = value->get_bool_component(j);
				break;
			default:
				/* FINISHME: What to do?  Exceptions are not the answer.
				*/
				break;
			}

			i++;
			if (i >= type->components())
				break;
		}

		value = (ir_constant *)value->next;
	}
}

ir_constant *
ir_constant::zero(void *mem_ctx, const glsl_type *type)
{
	check(type->is_numeric() || type->is_boolean());

	ir_constant *c = new(mem_ctx)ir_constant;
	c->type = type;
	memset(&c->value, 0, sizeof(c->value));

	return c;
}

bool
ir_constant::get_bool_component(unsigned i) const
{
	switch (this->type->base_type)
	{
	case GLSL_TYPE_UINT:  return this->value.u[i] != 0;
	case GLSL_TYPE_INT:   return this->value.i[i] != 0;
	case GLSL_TYPE_HALF: return ((int)this->value.f[i]) != 0;
	case GLSL_TYPE_FLOAT: return ((int)this->value.f[i]) != 0;
	case GLSL_TYPE_BOOL:  return this->value.b[i];
	default:              check(!"Should not get here."); break;
	}

	/* Must return something to make the compiler happy.  This is clearly an
	* error case.
	*/
	return false;
}

float
ir_constant::get_float_component(unsigned i) const
{
	switch (this->type->base_type)
	{
	case GLSL_TYPE_UINT:  return (float) this->value.u[i];
	case GLSL_TYPE_INT:   return (float) this->value.i[i];
	case GLSL_TYPE_HALF: return this->value.f[i];
	case GLSL_TYPE_FLOAT: return this->value.f[i];
	case GLSL_TYPE_BOOL:  return this->value.b[i] ? 1.0 : 0.0;
	default:              check(!"Should not get here."); break;
	}

	/* Must return something to make the compiler happy.  This is clearly an
	* error case.
	*/
	return 0.0;
}

int
ir_constant::get_int_component(unsigned i) const
{
	switch (this->type->base_type)
	{
	case GLSL_TYPE_UINT:  return this->value.u[i];
	case GLSL_TYPE_INT:   return this->value.i[i];
	case GLSL_TYPE_HALF: return (int) this->value.f[i];
	case GLSL_TYPE_FLOAT: return (int) this->value.f[i];
	case GLSL_TYPE_BOOL:  return this->value.b[i] ? 1 : 0;
	default:              check(!"Should not get here."); break;
	}

	/* Must return something to make the compiler happy.  This is clearly an
	* error case.
	*/
	return 0;
}

unsigned
ir_constant::get_uint_component(unsigned i) const
{
	switch (this->type->base_type)
	{
	case GLSL_TYPE_UINT:  return this->value.u[i];
	case GLSL_TYPE_INT:   return this->value.i[i];
	case GLSL_TYPE_HALF: return (unsigned) this->value.f[i];
	case GLSL_TYPE_FLOAT: return (unsigned) this->value.f[i];
	case GLSL_TYPE_BOOL:  return this->value.b[i] ? 1 : 0;
	default:              check(!"Should not get here."); break;
	}

	/* Must return something to make the compiler happy.  This is clearly an
	* error case.
	*/
	return 0;
}

ir_constant *
ir_constant::get_array_element(unsigned i) const
{
	check(this->type->is_array());

	/* From page 35 (page 41 of the PDF) of the GLSL 1.20 spec:
	*
	*     "Behavior is undefined if a shader subscripts an array with an index
	*     less than 0 or greater than or equal to the size the array was
	*     declared with."
	*
	* Most out-of-bounds accesses are removed before things could get this far.
	* There are cases where non-constant array index values can get constant
	* folded.
	*/
	if (int(i) < 0)
		i = 0;
	else if (i >= this->type->length)
		i = this->type->length - 1;

	return array_elements[i];
}

ir_constant *
ir_constant::get_record_field(const char *name)
{
	int idx = this->type->field_index(name);

	if (idx < 0)
		return NULL;

	if (this->components.is_empty())
		return NULL;

	exec_node *node = this->components.head;
	for (int i = 0; i < idx; i++)
	{
		node = node->next;

		/* If the end of the list is encountered before the element matching the
		* requested field is found, return NULL.
		*/
		if (node->is_tail_sentinel())
			return NULL;
	}

	return (ir_constant *)node;
}


bool
ir_constant::has_value(const ir_constant *c) const
{
	if (this->type != c->type)
		return false;

	if (this->type->is_array())
	{
		for (unsigned i = 0; i < this->type->length; i++)
		{
			if (!this->array_elements[i]->has_value(c->array_elements[i]))
				return false;
		}
		return true;
	}

	if (this->type->base_type == GLSL_TYPE_STRUCT)
	{
		const exec_node *a_node = this->components.head;
		const exec_node *b_node = c->components.head;

		while (!a_node->is_tail_sentinel())
		{
			check(!b_node->is_tail_sentinel());

			const ir_constant *const a_field = (ir_constant *)a_node;
			const ir_constant *const b_field = (ir_constant *)b_node;

			if (!a_field->has_value(b_field))
				return false;

			a_node = a_node->next;
			b_node = b_node->next;
		}

		return true;
	}

	for (unsigned i = 0; i < this->type->components(); i++)
	{
		switch (this->type->base_type)
		{
		case GLSL_TYPE_UINT:
			if (this->value.u[i] != c->value.u[i])
			{
				return false;
			}
			break;
		case GLSL_TYPE_INT:
			if (this->value.i[i] != c->value.i[i])
			{
				return false;
			}
			break;
		case GLSL_TYPE_HALF:
		case GLSL_TYPE_FLOAT:
			if (this->value.f[i] != c->value.f[i])
			{
				return false;
			}
			break;
		case GLSL_TYPE_BOOL:
			if (this->value.b[i] != c->value.b[i])
			{
				return false;
			}
			break;
		default:
			check(!"Should not get here.");
			return false;
		}
	}

	return true;
}

bool ir_constant::is_zero() const
{
	if (!this->type->is_scalar() && !this->type->is_vector())
	{
		return false;
	}

	for (unsigned c = 0; c < this->type->vector_elements; c++)
	{
		switch (this->type->base_type)
		{
		case GLSL_TYPE_HALF:
		case GLSL_TYPE_FLOAT:
			if (this->value.f[c] != 0.0)
			{
				return false;
			}
			break;
		case GLSL_TYPE_INT:
			if (this->value.i[c] != 0)
			{
				return false;
			}
			break;
		case GLSL_TYPE_UINT:
			if (this->value.u[c] != 0)
			{
				return false;
			}
			break;
		case GLSL_TYPE_BOOL:
			if (this->value.b[c] != false)
			{
				return false;
			}
			break;
		default:
			/* The only other base types are structures, arrays, and samplers.
			* Samplers cannot be constants, and the others should have been
			* filtered out above.
			*/
			check(!"Should not get here.");
			return false;
		}
	}

	return true;
}

bool
ir_constant::is_one() const
{
	if (!this->type->is_scalar() && !this->type->is_vector())
	{
		return false;
	}

	for (unsigned c = 0; c < this->type->vector_elements; c++)
	{
		switch (this->type->base_type)
		{
		case GLSL_TYPE_HALF:
		case GLSL_TYPE_FLOAT:
			if (this->value.f[c] != 1.0)
			{
				return false;
			}
			break;
		case GLSL_TYPE_INT:
			if (this->value.i[c] != 1)
			{
				return false;
			}
			break;
		case GLSL_TYPE_UINT:
			if (this->value.u[c] != 1)
			{
				return false;
			}
			break;
		case GLSL_TYPE_BOOL:
			if (this->value.b[c] != true)
			{
				return false;
			}
			break;
		default:
			/* The only other base types are structures, arrays, and samplers.
			* Samplers cannot be constants, and the others should have been
			* filtered out above.
			*/
			check(!"Should not get here.");
			return false;
		}
	}

	return true;
}

bool
ir_constant::is_negative_one() const
{
	if (!this->type->is_scalar() && !this->type->is_vector())
	{
		return false;
	}

	if (this->type->is_boolean())
	{
		return false;
	}

	for (unsigned c = 0; c < this->type->vector_elements; c++)
	{
		switch (this->type->base_type)
		{
		case GLSL_TYPE_HALF:
		case GLSL_TYPE_FLOAT:
			if (this->value.f[c] != -1.0)
			{
				return false;
			}
			break;
		case GLSL_TYPE_INT:
			if (this->value.i[c] != -1)
			{
				return false;
			}
			break;
		case GLSL_TYPE_UINT:
			if (int(this->value.u[c]) != -1)
			{
				return false;
			}
			break;
		default:
			/* The only other base types are structures, arrays, samplers, and
			* booleans.  Samplers cannot be constants, and the others should
			* have been filtered out above.
			*/
			check(!"Should not get here.");
			return false;
		}
	}

	return true;
}

static inline bool is_float_finite(float f)
{
	return ((*(unsigned int*)&f) & 0x7F800000) != 0x7F800000;
}

bool ir_constant::is_finite() const
{
	if (this->type->is_float())
	{
		unsigned num_components = type->components();
		for (unsigned i = 0; i < num_components; ++i)
		{
			if (!is_float_finite(value.f[i]))
			{
				return false;
			}
		}
	}

	return true;
}

bool ir_constant::is_component_finite(unsigned i) const
{
	if (this->type->is_float())
	{
		if (!is_float_finite(value.f[i]))
		{
			return false;
		}
	}
	return true;
}


bool ir_constant::are_any_zero() const
{
	unsigned num_components = type->components();

	if (this->type->is_float())
	{
		for (unsigned i = 0; i < num_components; ++i)
		{
			if (fabs(value.f[i]) < 1.e-8f)
			{
				return true;
			}
		}
	}
	else if (this->type->is_integer())
	{
		for (unsigned i = 0; i < num_components; ++i)
		{
			if (value.u[i] == 0)
			{
				return true;
			}
		}
	}
	else if (this->type->is_boolean())
	{
		for (unsigned i = 0; i < num_components; ++i)
		{
			if (value.b[i] == false)
			{
				return true;
			}
		}
	}

	return false;
}

bool ir_constant::IsEquivalent(ir_constant* IR)
{
	return has_value(IR);
}

ir_loop::ir_loop()
{
	this->ir_type = ir_type_loop;
	this->cmp = ir_unop_neg;
	this->from = NULL;
	this->to = NULL;
	this->increment = NULL;
	this->counter = NULL;
	this->mode = ir_loop::loop_dont_care;
	this->fastopt = 0;
	this->unroll_size = -1;
}


ir_dereference_variable::ir_dereference_variable(ir_variable *var)
{
	check(var != NULL);

	this->ir_type = ir_type_dereference_variable;
	this->var = var;
	this->type = var->type;
}


ir_dereference_array::ir_dereference_array(ir_rvalue* value,
	ir_rvalue* array_index)
{
	this->ir_type = ir_type_dereference_array;
	this->array_index = array_index;
	this->set_array(value);
}


ir_dereference_array::ir_dereference_array(ir_variable *var,
	ir_rvalue* array_index)
{
	void *ctx = ralloc_parent(var);

	this->ir_type = ir_type_dereference_array;
	this->array_index = array_index;
	this->set_array(new(ctx)ir_dereference_variable(var));
}


void
ir_dereference_array::set_array(ir_rvalue* value)
{
	check(value != NULL);

	this->array = value;

	const glsl_type *const vt = this->array->type;

	if (vt->is_array())
	{
		type = vt->element_type();
	}
	else if (vt->is_matrix())
	{
		type = vt->column_type();
	}
	else if (vt->is_vector())
	{
		type = vt->get_base_type();
	}
	else if (vt->is_patch())
	{
		type = vt->inner_type;
	}
}

bool ir_dereference_array::IsEquivalent(ir_dereference_array* IR)
{
	if (!AreEquivalent(array_index, IR->array_index))
	{
		return false;
	}

	return AreEquivalent(array, IR->array);
}


ir_dereference_image::ir_dereference_image(ir_rvalue* value,
	ir_rvalue* array_index)
{
	this->ir_type = ir_type_dereference_image;
	this->image_index = array_index;
	this->image = value;
	this->type = this->image->type->inner_type;
	this->op = ir_image_access;
}


ir_dereference_image::ir_dereference_image(ir_variable *var,
	ir_rvalue* array_index)
{
	void *ctx = ralloc_parent(var);

	this->ir_type = ir_type_dereference_image;
	this->image_index = array_index;
	this->image = new(ctx)ir_dereference_variable(var);
	this->type = this->image->type->inner_type;
	this->op = ir_image_access;
}

ir_dereference_image::ir_dereference_image(ir_rvalue* value,
	ir_rvalue* array_index, ir_image_op op)
{
	this->ir_type = ir_type_dereference_image;
	this->image_index = array_index;
	this->image = value;
	this->type = this->image->type->inner_type;
	this->op = op;
}


ir_dereference_image::ir_dereference_image(ir_variable *var,
	ir_rvalue* array_index, ir_image_op op)
{
	void *ctx = ralloc_parent(var);

	this->ir_type = ir_type_dereference_image;
	this->image_index = array_index;
	this->image = new(ctx)ir_dereference_variable(var);
	this->type = this->image->type->inner_type;
	this->op = op;
}

bool ir_dereference_image::IsEquivalent(ir_dereference_image* IR)
{
	if (op != IR->op)
	{
		return false;
	}

	return AreEquivalent(image, IR->image) && AreEquivalent(image_index, IR->image_index);
}

ir_dereference_record::ir_dereference_record(ir_rvalue* value,
	const char *field)
{
	check(value != NULL);

	this->ir_type = ir_type_dereference_record;
	this->record = value;
	this->field = ralloc_strdup(this, field);
	this->type = this->record->type->field_type(field);
}


ir_dereference_record::ir_dereference_record(ir_variable *var,
	const char *field)
{
	void *ctx = ralloc_parent(var);

	this->ir_type = ir_type_dereference_record;
	this->record = new(ctx)ir_dereference_variable(var);
	this->field = ralloc_strdup(this, field);
	this->type = this->record->type->field_type(field);
}

bool ir_dereference_record::IsEquivalent(ir_dereference_record* IR)
{
	check(field && IR->field);
	if (strcmp(field, IR->field))
	{
		return false;
	}

	return AreEquivalent(record, IR->record);
}

bool
ir_dereference::is_lvalue() const
{
	ir_variable *var = this->variable_referenced();

	/* Every l-value derference chain eventually ends in a variable.
	*/
	if ((var == NULL) || var->read_only)
		return false;

	/* From page 17 (page 23 of the PDF) of the GLSL 1.20 spec:
	*
	*    "Samplers cannot be treated as l-values; hence cannot be used
	*     as out or inout function parameters, nor can they be
	*     assigned into."
	*/

	// Commenting this out - because samplers in HLSL actually get assigned and passed as parameters to internal functions, and
	// eventually get optimized out and simplified to references to uniform variable, so it's too early to shout errors.

	//   if (this->type->contains_sampler())
	//      return false;

	return true;
}


const char *tex_opcode_strs[] = { "tex", "txb", "txl", "txd", "txf", "txs", "txg", "txm" };

const char *ir_texture::opcode_string()
{
	check((unsigned int)op <=
		sizeof(tex_opcode_strs) / sizeof(tex_opcode_strs[0]));
	return tex_opcode_strs[op];
}

ir_texture_opcode
ir_texture::get_opcode(const char *str)
{
	const int count = sizeof(tex_opcode_strs) / sizeof(tex_opcode_strs[0]);
	for (int op = 0; op < count; op++)
	{
		if (strcmp(str, tex_opcode_strs[op]) == 0)
			return (ir_texture_opcode)op;
	}
	return (ir_texture_opcode)-1;
}


void
ir_texture::set_sampler(ir_dereference *sampler, const glsl_type *type)
{
	check(sampler != NULL);
	check(type != NULL);
	this->sampler = sampler;
	this->type = type;

	if (this->op == ir_txs || this->op == ir_txm)
	{
		check(type->base_type == GLSL_TYPE_INT);
	}
	else
	{
		check(sampler->type->inner_type->base_type == (int)type->base_type);
		if (sampler->type->sampler_shadow)
		{
			check(type->vector_elements == 4 || type->vector_elements == 1);
		}
		else
		{
			check(type->vector_elements == 4);
		}
	}
}

bool ir_texture::IsEquivalent(ir_texture* IR)
{
	if (op != IR->op || channel != IR->channel)
	{
		return false;
	}

	if (!AreEquivalent(sampler, IR->sampler))
	{
		return false;
	}

	if (!AreEquivalent(coordinate, IR->coordinate))
	{
		return false;
	}

	if (!AreEquivalent(projector, IR->projector))
	{
		return false;
	}

	if (!AreEquivalent(shadow_comparitor, IR->shadow_comparitor))
	{
		return false;
	}

	if (!AreEquivalent(offset, IR->offset))
	{
		return false;
	}

	if (!AreEquivalent(lod_info.grad.dPdy, IR->lod_info.grad.dPdy))
	{
		return false;
	}

	if (!AreEquivalent(lod_info.grad.dPdx, IR->lod_info.grad.dPdx))
	{
		return false;
	}

	if (!AreEquivalent(SamplerState, IR->SamplerState))
	{
		return false;
	}

	return true;
}


void
ir_swizzle::init_mask(const unsigned *comp, unsigned count)
{
	check((count >= 1) && (count <= 4));

	memset(&this->mask, 0, sizeof(this->mask));
	this->mask.num_components = count;

	unsigned dup_mask = 0;
	switch (count)
	{
	case 4:
		check(comp[3] <= 3);
		dup_mask |= (1U << comp[3])
			& ((1U << comp[0]) | (1U << comp[1]) | (1U << comp[2]));
		this->mask.w = comp[3];

	case 3:
		check(comp[2] <= 3);
		dup_mask |= (1U << comp[2])
			& ((1U << comp[0]) | (1U << comp[1]));
		this->mask.z = comp[2];

	case 2:
		check(comp[1] <= 3);
		dup_mask |= (1U << comp[1])
			& ((1U << comp[0]));
		this->mask.y = comp[1];

	case 1:
		check(comp[0] <= 3);
		this->mask.x = comp[0];
	}

	this->mask.has_duplicates = dup_mask != 0;

	/* Based on the number of elements in the swizzle and the base type
	* (i.e., float, int, unsigned, or bool) of the vector being swizzled,
	* generate the type of the resulting value.
	*/
	type = glsl_type::get_instance(val->type->base_type, mask.num_components, 1);
}

ir_swizzle::ir_swizzle(ir_rvalue* val, unsigned x, unsigned y, unsigned z,
	unsigned w, unsigned count)
	: val(val)
{
	const unsigned components[4] = { x, y, z, w };
	this->ir_type = ir_type_swizzle;
	this->init_mask(components, count);
}

ir_swizzle::ir_swizzle(ir_rvalue* val, const unsigned *comp,
	unsigned count)
	: val(val)
{
	this->ir_type = ir_type_swizzle;
	this->init_mask(comp, count);
}

ir_swizzle::ir_swizzle(ir_rvalue* val, ir_swizzle_mask mask)
{
	this->ir_type = ir_type_swizzle;
	this->val = val;
	this->mask = mask;
	this->type = glsl_type::get_instance(val->type->base_type,
		mask.num_components, 1);
}

#define X 1
#define R 5
#define S 9
#define I 13

ir_swizzle* ir_swizzle::create(ir_rvalue* val, const char *str, unsigned vector_length)
{
	void *ctx = ralloc_parent(val);

	/* For each possible swizzle character, this table encodes the value in
	* \c idx_map that represents the 0th element of the vector.  For invalid
	* swizzle characters (e.g., 'k'), a special value is used that will allow
	* detection of errors.
	*/
	static const unsigned char base_idx[26] =
	{
		/* a  b  c  d  e  f  g  h  i  j  k  l  m */
		R, R, I, I, I, I, R, I, I, I, I, I, I,
		/* n  o  p  q  r  s  t  u  v  w  x  y  z */
		I, I, S, S, R, S, S, I, I, X, X, X, X
	};

	/* Each valid swizzle character has an entry in the previous table.  This
	* table encodes the base index encoded in the previous table plus the actual
	* index of the swizzle character.  When processing swizzles, the first
	* character in the string is indexed in the previous table.  Each character
	* in the string is indexed in this table, and the value found there has the
	* value form the first table subtracted.  The result must be on the range
	* [0,3].
	*
	* For example, the string "wzyx" will get X from the first table.  Each of
	* the charcaters will get X+3, X+2, X+1, and X+0 from this table.  After
	* subtraction, the swizzle values are { 3, 2, 1, 0 }.
	*
	* The string "wzrg" will get X from the first table.  Each of the characters
	* will get X+3, X+2, R+0, and R+1 from this table.  After subtraction, the
	* swizzle values are { 3, 2, 4, 5 }.  Since 4 and 5 are outside the range
	* [0,3], the error is detected.
	*/
	static const unsigned char idx_map[26] =
	{
		/* a    b    c    d    e    f    g    h    i    j    k    l    m */
		R + 3, R + 2, 0, 0, 0, 0, R + 1, 0, 0, 0, 0, 0, 0,
		/* n    o    p    q    r    s    t    u    v    w    x    y    z */
		0, 0, S + 2, S + 3, R + 0, S + 0, S + 1, 0, 0, X + 3, X + 0, X + 1, X + 2
	};

	int swiz_idx[4] = { 0, 0, 0, 0 };
	unsigned i;


	/* Validate the first character in the swizzle string and look up the base
	* index value as described above.
	*/
	if ((str[0] < 'a') || (str[0] > 'z'))
		return NULL;

	const unsigned base = base_idx[str[0] - 'a'];


	for (i = 0; (i < 4) && (str[i] != '\0'); i++)
	{
		/* Validate the next character, and, as described above, convert it to a
		* swizzle index.
		*/
		if ((str[i] < 'a') || (str[i] > 'z'))
		{
			return NULL;
		}

		swiz_idx[i] = idx_map[str[i] - 'a'] - base;
		if ((swiz_idx[i] < 0) || (swiz_idx[i] >= (int)vector_length))
		{
			return NULL;
		}
	}

	if (str[i] != '\0')
	{
		return NULL;
	}

	return new(ctx)ir_swizzle(val, swiz_idx[0], swiz_idx[1], swiz_idx[2], swiz_idx[3], i);
}

#undef X
#undef R
#undef S
#undef I

ir_variable *
ir_swizzle::variable_referenced() const
{
	return this->val->variable_referenced();
}

bool ir_swizzle::IsEquivalent(ir_swizzle* IR)
{
	if (mask.num_components != IR->mask.num_components)
	{
		return false;
	}

	if (mask.x != IR->mask.x ||
		mask.y != IR->mask.y ||
		mask.z != IR->mask.z ||
		mask.w != IR->mask.w)
	{
		return false;
	}

	return AreEquivalent(val, IR->val);
}


ir_variable::ir_variable(const struct glsl_type *type, const char *name,
	ir_variable_mode mode)
	: semantic(NULL), max_array_access(0), read_only(false), centroid(false), invariant(false),
	used(false), mode(mode), interpolation(ir_interp_qualifier_none),
	geometryinput(ir_geometry_input_none), is_interface_block(false), origin_upper_left(false),
	pixel_center_integer(false), explicit_location(false), has_initializer(false),
	has_been_sampled(false), image_read(false), image_write(false), coherent(false),
	is_patch_constant(false), depth_layout(ir_depth_layout_none), location(-1),
	num_state_slots(0), state_slots(NULL), warn_extension(NULL), constant_value(NULL),
	constant_initializer(NULL)
{
	this->ir_type = ir_type_variable;
	this->type = type;
	this->name = ralloc_strdup(this, name);

	if (type && type->base_type == GLSL_TYPE_SAMPLER)
	{
		this->read_only = true;
	}
}


const char *
ir_variable::interpolation_string() const
{
	switch (this->interpolation)
	{
	case ir_interp_qualifier_none:          return "no";
	case ir_interp_qualifier_smooth:        return "smooth";
	case ir_interp_qualifier_flat:          return "flat";
	case ir_interp_qualifier_noperspective: return "noperspective";
	}

	check(!"Should not get here.");
	return "";
}

bool ir_variable::IsEquivalent(ir_variable* IR)
{
	return this == IR;
}


ir_function_signature::ir_function_signature(const glsl_type *return_type)
: return_type(return_type), is_defined(false), _function(NULL),
is_early_depth_stencil(0), wg_size_x(0), wg_size_y(0), wg_size_z(0)
{
	this->ir_type = ir_type_function_signature;
	this->is_builtin = false;
	this->has_output_parameters = false;
	this->is_main = false;
	this->return_semantic = NULL;
	this->maxvertexcount = 0;
}


static bool
modes_match(unsigned a, unsigned b)
{
	if (a == b)
		return true;

	/* Accept "in" vs. "const in" */
	if ((a == ir_var_const_in && b == ir_var_in) ||
		(b == ir_var_const_in && a == ir_var_in))
	{
		return true;
	}

	return false;
}


const char *
ir_function_signature::qualifiers_match(exec_list *params)
{
	exec_list_iterator iter_a = parameters.iterator();
	exec_list_iterator iter_b = params->iterator();

	/* check that the qualifiers match. */
	while (iter_a.has_next())
	{
		ir_variable *a = (ir_variable *)iter_a.get();
		ir_variable *b = (ir_variable *)iter_b.get();

		if (a->read_only != b->read_only ||
			!modes_match(a->mode, b->mode) ||
			a->interpolation != b->interpolation ||
			a->geometryinput != b->geometryinput ||
			a->centroid != b->centroid ||
			a->is_patch_constant != b->is_patch_constant)
		{

			/* parameter a's qualifiers don't match */
			return a->name;
		}

		iter_a.next();
		iter_b.next();
	}
	return NULL;
}


void
ir_function_signature::replace_parameters(exec_list *new_params)
{
	/* Destroy all of the previous parameter information.  If the previous
	* parameter information comes from the function prototype, it may either
	* specify incorrect parameter names or not have names at all.
	*/
	foreach_iter(exec_list_iterator, iter, parameters)
	{
		check(((ir_instruction *)iter.get())->as_variable() != NULL);

		iter.remove();
	}

	new_params->move_nodes_to(&parameters);
}


ir_function::ir_function(const char *name)
{
	this->ir_type = ir_type_function;
	this->name = ralloc_strdup(this, name);
}


bool
ir_function::has_user_signature()
{
	foreach_list(n, &this->signatures)
	{
		ir_function_signature *const sig = (ir_function_signature *)n;
		if (!sig->is_builtin)
		{
			return true;
		}
	}
	return false;
}

const char *
ir_atomic::operator_string()
{
	static const char* str[] =
	{
		"atomic_add",
		"atomic_and",
		"atomic_min",
		"atomic_max",
		"atomic_or",
		"atomic_xor",
		"atomic_swap",
		"atomic_cmp_swap",
		"atomic_load",
		"atomic_store"
	};
	static_assert(GetNumArrayElements(str) == ir_atomic_count, "Mismatched atomic count");
	return str[this->operation];
}

bool ir_atomic::IsEquivalent(ir_atomic* IR)
{
	if (operation != IR->operation)
	{
		return false;
	}

	if (!AreEquivalent(memory_ref, IR->memory_ref))
	{
		return false;
	}

	if (!AreEquivalent(lhs, IR->lhs))
	{
		return false;
	}

	return AreEquivalent(operands[0], IR->operands[0]) && AreEquivalent(operands[1], IR->operands[1]);
}


ir_rvalue* ir_rvalue::error_value(void *mem_ctx)
{
	ir_rvalue* v = new(mem_ctx)ir_rvalue;

	v->type = glsl_type::error_type;
	return v;
}

ir_rvalue*  ir_rvalue::void_value(void *mem_ctx)
{
	ir_rvalue* v = new(mem_ctx)ir_rvalue;
	v->type = glsl_type::void_type;
	return v;
}


void visit_exec_list(exec_list *list, ir_visitor *visitor)
{
	foreach_iter(exec_list_iterator, iter, *list)
	{
		ir_instruction* Instruction = ((ir_instruction *)iter.get());
		Instruction->accept(visitor);
	}
}

static ir_rvalue* try_min_one(ir_rvalue* ir)
{
	ir_expression *expr = ir->as_expression();

	if (!expr || expr->operation != ir_binop_min)
	{
		return NULL;
	}

	if (expr->operands[0]->is_one())
	{
		return expr->operands[1];
	}

	if (expr->operands[1]->is_one())
	{
		return expr->operands[0];
	}

	return NULL;
}

static ir_rvalue* try_max_zero(ir_rvalue* ir)
{
	ir_expression *expr = ir->as_expression();

	if (!expr || expr->operation != ir_binop_max)
	{
		return NULL;
	}

	if (expr->operands[0]->is_zero())
	{
		return expr->operands[1];
	}

	if (expr->operands[1]->is_zero())
	{
		return expr->operands[0];
	}

	return NULL;
}

ir_rvalue* ir_rvalue::as_rvalue_to_saturate()
{
	ir_expression *expr = this->as_expression();

	if (!expr)
	{
		return NULL;
	}

	ir_rvalue* max_zero = try_max_zero(expr);
	if (max_zero)
	{
		return try_min_one(max_zero);
	}
	else
	{
		ir_rvalue* min_one = try_min_one(expr);
		if (min_one)
		{
			return try_max_zero(min_one);
		}
	}

	return NULL;
}


bool AreEquivalent(ir_instruction* A, ir_instruction* B)
{
	if (A == B)
	{
		return true;
	}

	if (A == NULL || B == NULL)
	{
		return false;
	}

#if ! (defined(__clang__) || defined(__GNUC__))
#define IF_TEST(name)		if (A->##name()) { if (A->##name() && B->##name()) { return (A->##name())->IsEquivalent(B->##name()); } }
#else
#define IF_TEST(name)		if (A->name()) { if (A->name() && B->name()) { return (A->name())->IsEquivalent(B->name()); } }
#endif
	IF_TEST(as_dereference_array)
else IF_TEST(as_dereference_image)
	else IF_TEST(as_dereference_record)
	else IF_TEST(as_dereference_variable)
	else IF_TEST(as_expression)
	else IF_TEST(as_constant)
	/*
	// TODO as these require Equivalence on exec_node lists
	else IF_TEST(as_function)
	else IF_TEST(as_rvalue)
	else IF_TEST(as_loop)
	else IF_TEST(as_call)
	else IF_TEST(as_if)
	*/
	else IF_TEST(as_assignment)
	else IF_TEST(as_return)
	else IF_TEST(as_swizzle)
	else IF_TEST(as_discard)
	else IF_TEST(as_atomic)
	else IF_TEST(as_texture)
	else IF_TEST(as_variable)
	else
	{
		// Missed type!
		check(0);
	}

	return false;
}

unsigned ir_hash_table_pointer_hash(const void *key)
{
	const ir_instruction* key_instr = (const ir_instruction*)key;
	return key_instr ? (unsigned)key_instr->id : ~0u;
}

int ir_hash_table_pointer_compare(const void *key1, const void *key2)
{
	const ir_instruction* key1_instr = (const ir_instruction*)key1;
	const ir_instruction* key2_instr = (const ir_instruction*)key2;
	return (key1_instr == key2_instr || (key1_instr && key2_instr && key1_instr->id == key2_instr->id)) ? 0 : 1;
}
