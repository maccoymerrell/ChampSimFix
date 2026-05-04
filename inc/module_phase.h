/*
 *    Copyright 2026 The ChampSim Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef MODULE_PHASE_H
#define MODULE_PHASE_H

namespace champsim
{

/**
 * Opt-in interface for modules that want to participate in phase
 * orchestration.
 *
 * Inherit from this in addition to your module interface (e.g.
 * ``cache_module``, ``core_module``, ``memory_controller_module``) when
 * your module needs phase-edge notifications. The phase orchestrator
 * discovers participants via the interface registry and drives every
 * hook on each registered module.
 *
 * The warmup and ROI flags are passed into ``begin_phase`` as
 * arguments. They are intentionally not stored on the interface so that
 * a phase controller cannot accidentally flip them mid-phase. Modules
 * that need these flags later should copy them into their own state
 * inside ``begin_phase``.
 *
 * Both hooks are pure-virtual on purpose: opting in is an explicit
 * promise to handle every callback, so users cannot silently mistype an
 * override. Modules that do not need phase notifications simply do not
 * inherit ``module_phase``.
 */
struct module_phase
{
  virtual ~module_phase() = default;

  /**
   * Called at the start of each phase, before any cycles run.
   *
   * \param warmup True for the warmup phase, false otherwise.
   * \param roi    True if this phase contributes to region-of-interest
   *               statistics; typically the inverse of ``warmup`` but
   *               not required to be.
   */
  virtual void begin_phase(bool warmup, bool roi) = 0;

  /** Called at the end of each phase, after all cycles complete. */
  virtual void end_phase() = 0;
};

} // namespace champsim

#endif // MODULE_PHASE_H
