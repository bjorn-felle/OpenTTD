/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file newgrf_spritegroup.cpp Handling of primarily NewGRF action 2. */

#include "stdafx.h"
#include "debug.h"
#include "newgrf_spritegroup.h"
#include "newgrf_profiling.h"
#include "core/pool_func.hpp"

#include "safeguards.h"

SpriteGroupPool _spritegroup_pool("SpriteGroup");
INSTANTIATE_POOL_METHODS(SpriteGroup)

/* static */ TemporaryStorageArray<int32_t, 0x110> ResolverObject::temp_store;


/**
 * ResolverObject (re)entry point.
 * This cannot be made a call to a virtual function because virtual functions
 * do not like nullptr and checking for nullptr *everywhere* is more cumbersome than
 * this little helper function.
 * @param group the group to resolve for
 * @param object information needed to resolve the group
 * @param top_level true if this is a top-level SpriteGroup, false if used nested in another SpriteGroup.
 * @return the resolved group
 */
/* static */ ResolverResult SpriteGroup::Resolve(const SpriteGroup *group, ResolverObject &object, bool top_level)
{
	if (group == nullptr) return std::monostate{};

	const GRFFile *grf = object.grffile;
	auto profiler = std::ranges::find(_newgrf_profilers, grf, &NewGRFProfiler::grffile);

	if (profiler == _newgrf_profilers.end() || !profiler->active) {
		return group->Resolve(object);
	} else if (top_level) {
		profiler->BeginResolve(object);
		auto result = group->Resolve(object);
		profiler->EndResolve(result);
		return result;
	} else {
		profiler->RecursiveResolve();
		return group->Resolve(object);
	}
}

static inline uint32_t GetVariable(const ResolverObject &object, ScopeResolver *scope, uint8_t variable, uint32_t parameter, bool &available)
{
	uint32_t value;
	switch (variable) {
		case 0x0C: return object.callback;
		case 0x10: return object.callback_param1;
		case 0x18: return object.callback_param2;
		case 0x1C: return object.last_value;

		case 0x5F: return (scope->GetRandomBits() << 8) | scope->GetRandomTriggers();

		case 0x7D: return object.GetRegister(parameter);

		case 0x7F:
			if (object.grffile == nullptr) return 0;
			return object.grffile->GetParam(parameter);

		default:
			/* First handle variables common with Action7/9/D */
			if (variable < 0x40 && GetGlobalVariable(variable, &value, object.grffile)) return value;
			/* Not a common variable, so evaluate the feature specific variables */
			return scope->GetVariable(variable, parameter, available);
	}
}

/**
 * Get a few random bits. Default implementation has no random bits.
 * @return Random bits.
 */
/* virtual */ uint32_t ScopeResolver::GetRandomBits() const
{
	return 0;
}

/**
 * Get the triggers. Base class returns \c 0 to prevent trouble.
 * @return The triggers.
 */
/* virtual */ uint32_t ScopeResolver::GetRandomTriggers() const
{
	return 0;
}

/**
 * Get a variable value. Default implementation has no available variables.
 * @param variable Variable to read
 * @param parameter Parameter for 60+x variables
 * @param[out] available Set to false, in case the variable does not exist.
 * @return Value
 */
/* virtual */ uint32_t ScopeResolver::GetVariable(uint8_t variable, [[maybe_unused]] uint32_t parameter, bool &available) const
{
	Debug(grf, 1, "Unhandled scope variable 0x{:X}", variable);
	available = false;
	return UINT_MAX;
}

/**
 * Store a value into the persistent storage area (PSA). Default implementation does nothing (for newgrf classes without storage).
 */
/* virtual */ void ScopeResolver::StorePSA(uint, int32_t) {}

/**
 * Get the real sprites of the grf.
 * @param group Group to get.
 * @return The available sprite group.
 */
/* virtual */ const SpriteGroup *ResolverObject::ResolveReal(const RealSpriteGroup &group) const
{
	if (!group.loaded.empty()) return group.loaded[0];
	if (!group.loading.empty()) return group.loading[0];

	return nullptr;
}

/**
 * Get a resolver for the \a scope.
 * @return The resolver for the requested scope.
 */
/* virtual */ ScopeResolver *ResolverObject::GetScope(VarSpriteGroupScope, uint8_t)
{
	return &this->default_scope;
}

/* Evaluate an adjustment for a variable of the given size.
 * U is the unsigned type and S is the signed type to use. */
template <typename U, typename S>
static U EvalAdjustT(const DeterministicSpriteGroupAdjust &adjust, ResolverObject &object, ScopeResolver *scope, U last_value, uint32_t value)
{
	value >>= adjust.shift_num;
	value  &= adjust.and_mask;

	switch (adjust.type) {
		case DSGA_TYPE_DIV:  value = ((S)value + (S)adjust.add_val) / (S)adjust.divmod_val; break;
		case DSGA_TYPE_MOD:  value = ((S)value + (S)adjust.add_val) % (S)adjust.divmod_val; break;
		case DSGA_TYPE_NONE: break;
	}

	switch (adjust.operation) {
		case DSGA_OP_ADD:  return last_value + value;
		case DSGA_OP_SUB:  return last_value - value;
		case DSGA_OP_SMIN: return std::min<S>(last_value, value);
		case DSGA_OP_SMAX: return std::max<S>(last_value, value);
		case DSGA_OP_UMIN: return std::min<U>(last_value, value);
		case DSGA_OP_UMAX: return std::max<U>(last_value, value);
		case DSGA_OP_SDIV: return value == 0 ? (S)last_value : (S)last_value / (S)value;
		case DSGA_OP_SMOD: return value == 0 ? (S)last_value : (S)last_value % (S)value;
		case DSGA_OP_UDIV: return value == 0 ? (U)last_value : (U)last_value / (U)value;
		case DSGA_OP_UMOD: return value == 0 ? (U)last_value : (U)last_value % (U)value;
		case DSGA_OP_MUL:  return last_value * value;
		case DSGA_OP_AND:  return last_value & value;
		case DSGA_OP_OR:   return last_value | value;
		case DSGA_OP_XOR:  return last_value ^ value;
		case DSGA_OP_STO:  object.SetRegister((U)value, (S)last_value); return last_value;
		case DSGA_OP_RST:  return value;
		case DSGA_OP_STOP: scope->StorePSA((U)value, (S)last_value); return last_value;
		case DSGA_OP_ROR:  return std::rotr<uint32_t>((U)last_value, (U)value & 0x1F); // mask 'value' to 5 bits, which should behave the same on all architectures.
		case DSGA_OP_SCMP: return ((S)last_value == (S)value) ? 1 : ((S)last_value < (S)value ? 0 : 2);
		case DSGA_OP_UCMP: return ((U)last_value == (U)value) ? 1 : ((U)last_value < (U)value ? 0 : 2);
		case DSGA_OP_SHL:  return (uint32_t)(U)last_value << ((U)value & 0x1F); // Same behaviour as in ParamSet, mask 'value' to 5 bits, which should behave the same on all architectures.
		case DSGA_OP_SHR:  return (uint32_t)(U)last_value >> ((U)value & 0x1F);
		case DSGA_OP_SAR:  return (int32_t)(S)last_value >> ((U)value & 0x1F);
		default:           return value;
	}
}


static bool RangeHighComparator(const DeterministicSpriteGroupRange &range, uint32_t value)
{
	return range.high < value;
}

/* virtual */ ResolverResult DeterministicSpriteGroup::Resolve(ResolverObject &object) const
{
	uint32_t last_value = 0;
	uint32_t value = 0;

	ScopeResolver *scope = object.GetScope(this->var_scope);

	for (const auto &adjust : this->adjusts) {
		/* Try to get the variable. We shall assume it is available, unless told otherwise. */
		bool available = true;
		if (adjust.variable == 0x7E) {
			auto subgroup = SpriteGroup::Resolve(adjust.subroutine, object, false);
			auto *subvalue = std::get_if<CallbackResult>(&subgroup);
			value = subvalue != nullptr ? *subvalue : UINT16_MAX;

			/* Note: 'last_value' and 'reseed' are shared between the main chain and the procedure */
		} else if (adjust.variable == 0x7B) {
			value = GetVariable(object, scope, adjust.parameter, last_value, available);
		} else {
			value = GetVariable(object, scope, adjust.variable, adjust.parameter, available);
		}

		if (!available) {
			/* Unsupported variable: skip further processing and return either
			 * the group from the first range or the default group. */
			return SpriteGroup::Resolve(this->error_group, object, false);
		}

		switch (this->size) {
			case DSG_SIZE_BYTE:  value = EvalAdjustT<uint8_t,  int8_t> (adjust, object, scope, last_value, value); break;
			case DSG_SIZE_WORD:  value = EvalAdjustT<uint16_t, int16_t>(adjust, object, scope, last_value, value); break;
			case DSG_SIZE_DWORD: value = EvalAdjustT<uint32_t, int32_t>(adjust, object, scope, last_value, value); break;
			default: NOT_REACHED();
		}
		last_value = value;
	}

	object.last_value = last_value;

	auto result = this->default_result;

	if (this->ranges.size() > 4) {
		const auto &lower = std::lower_bound(this->ranges.begin(), this->ranges.end(), value, RangeHighComparator);
		if (lower != this->ranges.end() && lower->low <= value) {
			assert(lower->low <= value && value <= lower->high);
			result = lower->result;
		}
	} else {
		for (const auto &range : this->ranges) {
			if (range.low <= value && value <= range.high) {
				result = range.result;
				break;
			}
		}
	}

	if (result.calculated_result) {
		return static_cast<CallbackResult>(GB(value, 0, 15));
	}
	return SpriteGroup::Resolve(result.group, object, false);
}


/* virtual */ ResolverResult RandomizedSpriteGroup::Resolve(ResolverObject &object) const
{
	ScopeResolver *scope = object.GetScope(this->var_scope, this->count);
	if (object.callback == CBID_RANDOM_TRIGGER) {
		/* Handle triggers */
		uint8_t match = this->triggers & object.GetWaitingRandomTriggers();
		bool res = (this->cmp_mode == RSG_CMP_ANY) ? (match != 0) : (match == this->triggers);

		if (res) {
			object.AddUsedRandomTriggers(match);
			object.reseed[this->var_scope] |= (this->groups.size() - 1) << this->lowest_randbit;
		}
	}

	uint32_t mask = ((uint)this->groups.size() - 1) << this->lowest_randbit;
	uint8_t index = (scope->GetRandomBits() & mask) >> this->lowest_randbit;

	return SpriteGroup::Resolve(this->groups[index], object, false);
}

/* virtual */ ResolverResult CallbackResultSpriteGroup::Resolve(ResolverObject &) const
{
	return this->result;
}

/* virtual */ ResolverResult RealSpriteGroup::Resolve(ResolverObject &object) const
{
	/* Call the feature specific evaluation via ResultSpriteGroup::ResolveReal.
	 * The result is either ResultSpriteGroup, CallbackResultSpriteGroup, or nullptr.
	 */
	return SpriteGroup::Resolve(object.ResolveReal(*this), object, false);
}

/**
 * Process registers and the construction stage into the sprite layout.
 * The passed construction stage might get reset to zero, if it gets incorporated into the layout
 * during the preprocessing.
 * @param object ResolverObject owning the temporary storage.
 * @param[in,out] stage Construction stage (0-3), or nullptr if not applicable.
 * @return sprite layout to draw.
 */
SpriteLayoutProcessor TileLayoutSpriteGroup::ProcessRegisters(const ResolverObject &object, uint8_t *stage) const
{
	if (!this->dts.NeedsPreprocessing()) {
		if (stage != nullptr && this->dts.consistent_max_offset > 0) *stage = GetConstructionStageOffset(*stage, this->dts.consistent_max_offset);
		return SpriteLayoutProcessor(this->dts);
	}

	uint8_t actual_stage = stage != nullptr ? *stage : 0;
	SpriteLayoutProcessor result(this->dts, 0, 0, 0, actual_stage, false);
	result.ProcessRegisters(object, 0, 0);

	/* Stage has been processed by PrepareLayout(), set it to zero. */
	if (stage != nullptr) *stage = 0;

	return result;
}
