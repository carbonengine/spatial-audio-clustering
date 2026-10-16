/*
 * Copyright 2026 CCP ehf.
 *
 * This software was developed by CCP Games for spatial audio object clustering
 * in EVE Online and EVE Frontier.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * This license does not grant any rights to CCP's trademarks or game content.
 * EVE Online and EVE Frontier are registered trademarks of CCP ehf.
 */

#pragma once

#include "ClusterEngine.h"

#include <climits>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

/** @brief Double precision vector for scenario motion. */
struct Vec3
{
    double x = 0.0; ///< X coordinate.
    double y = 0.0; ///< Y coordinate.
    double z = 0.0; ///< Z coordinate.
};

/** @brief How a scenario source moves over time. */
enum class Motion
{
    Static, ///< Stays at its origin.
    Orbit,  ///< Rotates around an axis through the listener.
    Linear  ///< Moves along a straight line.
};

/** @brief One sound source of a scenario. */
struct SourceSpec
{
    uint64_t key = 0;               ///< Input key.
    uint32_t group = 0;             ///< Input group.
    bool clusterable = true;        ///< False for passthrough inputs.
    uint32_t priority = 50;         ///< Input priority.
    float loudness = 0.1f;          ///< RMS level.
    int startFrame = 0;             ///< First frame in which the source plays.
    int endFrame = INT_MAX;         ///< Frame in which the source no longer plays.
    Motion motion = Motion::Static; ///< Motion model.
    Vec3 origin;                    ///< Position at frame zero.
    Vec3 axis;                      ///< Unit rotation axis of an orbiting source.
    double degreesPerSecond = 0.0;  ///< Rotation speed of an orbiting source.
    Vec3 velocity;                  ///< Velocity of a linear source in units per second.
    double jitterDegrees = 0.0;     ///< Largest random direction offset applied each frame.
};

/** @brief Parameters that apply from a given frame on. */
struct ParamChange
{
    int frame = 0;        ///< First frame with the new parameters.
    ClusterParams params; ///< New parameters.
};

/** @brief Complete description of a generated scenario. */
struct Scenario
{
    std::string name;                      ///< Scenario name.
    std::string description;               ///< One line description.
    uint32_t seed = 0;                     ///< Seed the scenario was generated from.
    double frameSeconds = 512.0 / 48000.0; ///< Frame length in seconds.
    int frames = 0;                        ///< Number of frames to run.
    int settleFrame = -1;                  ///< Frame after which no moves are expected, or -1.
    bool shuffle = false;                  ///< Shuffles the input order every frame.
    ClusterParams params;                  ///< Parameters at frame zero.
    std::vector<ParamChange> changes;      ///< Later parameter changes in frame order.
    std::vector<SourceSpec> sources;       ///< All sources.
};

/** @brief Returns the names of all scenarios. */
const std::vector<std::string>& GetScenarioNames();

/** @brief Returns the default seed of a scenario. */
uint32_t GetDefaultSeed(const std::string& in_name);

/** @brief Builds a scenario by name and returns false if the name is unknown. */
bool BuildScenario(const std::string& in_name, uint32_t in_seed, Scenario& out_scenario);

/** @brief Builds the benchmark scenario with exactly the given number of inputs in every frame. */
void BuildBenchScenario(uint32_t in_inputs, uint32_t in_seed, int in_frames, Scenario& out_scenario);

/** @brief Produces the engine inputs of a scenario frame by frame. */
class ScenarioPlayer
{
public:
    /** @brief Prepares playback of a scenario with a seed for the input order. */
    ScenarioPlayer(const Scenario& in_scenario, uint32_t in_orderSeed, bool in_shuffle);

    /** @brief Builds the inputs of the next frame. */
    const std::vector<ClusterInput>& NextFrame();

    /** @brief Returns the parameters of the frame last built. */
    const ClusterParams& GetParams() const;

    /** @brief Returns the index of the frame last built. */
    int GetFrame() const;

private:
    const Scenario& m_scenario;          ///< Scenario being played.
    std::mt19937 m_jitterRandom;         ///< Random source for direction jitter.
    std::mt19937 m_orderRandom;          ///< Random source for the input order.
    bool m_shuffle = false;              ///< Shuffles the input order every frame.
    std::vector<ClusterInput> m_inputs;  ///< Inputs of the frame last built.
    ClusterParams m_params;              ///< Parameters of the frame last built.
    size_t m_nextChange = 0;             ///< Index of the next parameter change.
    int m_frame = -1;                    ///< Index of the frame last built.
};
