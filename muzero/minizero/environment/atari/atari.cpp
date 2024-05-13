#include "atari.h"
#include <opencv2/opencv.hpp>
#include <utility>

namespace minizero::env::atari {

std::string getAtariActionName(int action_id)
{
    assert(action_id >= 0 && action_id < kAtariActionSize);
    std::string action_name = ale::action_to_string(ale::Action(action_id));
    return action_name.substr(action_name.find_last_of("_") + 1);
}

AtariEnv& AtariEnv::operator=(const AtariEnv& env)
{
    reset(env.seed_);
    for (const auto& action : env.getActionHistory()) { act(action); }
    return *this;
}

void AtariEnv::reset(int seed)
{
    turn_ = Player::kPlayer1;
    seed_ = seed;
    reward_ = 0;
    total_reward_ = 0;
    ale_.setInt("random_seed", seed_);
    ale_.setInt("max_num_frames_per_episode", kAtariMaxNumFramesPerEpisode);
    ale_.setFloat("repeat_action_probability", kAtariRepeatActionProbability);
    ale_.loadROM(config::env_atari_rom_dir + "/" + config::env_atari_name + ".bin");
    ale_.reset_game();
    minimal_action_set_.clear();
    for (auto action_id : ale_.getMinimalActionSet()) { minimal_action_set_.insert(action_id); }
    lives_history_.clear();
    lives_history_.push_back(ale_.lives());
    actions_.clear();
    observations_.clear();
    observations_.reserve(kAtariMaxNumFramesPerEpisode + 1);
    observations_.push_back(getObservationString()); // initial observation
    feature_history_.clear();
    feature_history_.resize(kAtariFeatureHistorySize, std::vector<float>(3 * kAtariResolution * kAtariResolution, 0.0f));
    feature_history_.push_back(getObservation()); // initial screen
    feature_history_.pop_front();
    action_feature_history_.clear();
    action_feature_history_.resize(kAtariFeatureHistorySize, std::vector<float>(kAtariResolution * kAtariResolution, 0.0f));
}

bool AtariEnv::act(const AtariAction& action)
{
    assert(action.getPlayer() == Player::kPlayer1);
    assert(action.getActionID() >= 0 && action.getActionID() < kAtariActionSize);

    reward_ = 0;
    for (int i = 0; i < kAtariFrameSkip; ++i) { reward_ += ale_.act(ale::Action(action.getActionID())); }
    total_reward_ += reward_;
    lives_history_.push_back(ale_.lives());
    actions_.push_back(action);
    observations_.push_back(getObservationString());
    // only keep the most recent N observations in atari games to save memory, N is determined by configuration
    size_t recent_observation_length = (config::zero_actor_intermediate_sequence_length == 0 ? kAtariMaxNumFramesPerEpisode : config::zero_actor_intermediate_sequence_length + kAtariFeatureHistorySize + config::learner_n_step_return) + 1; // plus 1 for initial observation
    if (observations_.size() > recent_observation_length) {
        observations_[observations_.size() - recent_observation_length].clear();
        observations_[observations_.size() - recent_observation_length].shrink_to_fit();
    }

    // action & observation history
    action_feature_history_.push_back(std::vector<float>(kAtariResolution * kAtariResolution, action.getActionID() * 1.0f / kAtariActionSize));
    action_feature_history_.pop_front();
    feature_history_.push_back(getObservation());
    feature_history_.pop_front();

    return true;
}

std::vector<AtariAction> AtariEnv::getLegalActions() const
{
    std::vector<AtariAction> legal_actions;
    for (int action_id = 0; action_id < kAtariActionSize; ++action_id) {
        AtariAction action(action_id, getTurn());
        if (isLegalAction(action)) { legal_actions.push_back(action); }
    }
    return legal_actions;
}

std::vector<float> AtariEnv::getFeatures(utils::Rotation rotation /* = utils::Rotation::kRotationNone */) const
{
    std::vector<float> features;
    features.reserve(kAtariFeatureHistorySize * 4 * kAtariResolution * kAtariResolution);
    for (int i = 0; i < kAtariFeatureHistorySize; ++i) { // 1 for action; 3 for RGB, action first since the latest observation didn't have action yet
        features.insert(features.end(), action_feature_history_[i].begin(), action_feature_history_[i].end());
        features.insert(features.end(), feature_history_[i].begin(), feature_history_[i].end());
    }
    assert(static_cast<int>(features.size()) == kAtariFeatureHistorySize * 4 * kAtariResolution * kAtariResolution);
    return features;
}

std::vector<float> AtariEnv::getActionFeatures(const AtariAction& action, utils::Rotation rotation /* = utils::Rotation::kRotationNone */) const
{
    int hidden_size = kAtariHiddenChannelHeight * kAtariHiddenChannelWidth;
    std::vector<float> action_features(kAtariActionSize * hidden_size, 0.0f);
    std::fill(action_features.begin() + action.getActionID() * hidden_size, action_features.begin() + (action.getActionID() + 1) * hidden_size, 1.0f);
    return action_features;
}

std::vector<float> AtariEnv::getObservation(bool scale_01 /* = true */) const
{
    // get current screen rgb
    std::vector<unsigned char> screen_rgb;
    ale_.getScreenRGB(screen_rgb);

    // resize observation
    cv::Mat source_matrix(ale_.getScreen().height(), ale_.getScreen().width(), CV_8UC3, screen_rgb.data());
    cv::Mat reshape_matrix;
    cv::resize(source_matrix, reshape_matrix, cv::Size(kAtariResolution, kAtariResolution), 0, 0, cv::INTER_AREA);

    // change hwc to chw
    std::vector<float> observation(3 * kAtariResolution * kAtariResolution);
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < kAtariResolution * kAtariResolution; ++j) {
            observation[i * kAtariResolution * kAtariResolution + j] = static_cast<float>(reshape_matrix.at<unsigned char>(j * 3 + i));
            if (scale_01) { observation[i * kAtariResolution * kAtariResolution + j] /= 255.0f; }
        }
    }
    return observation;
}

std::string AtariEnv::getObservationString() const
{
    std::string obs_string;
    std::vector<float> observation = getObservation(false);
    for (const auto& o : observation) {
        assert(o >= 0 && o < 256);
        obs_string += static_cast<char>(o);
    }
    return obs_string;
}

void AtariEnvLoader::reset()
{
    BaseEnvLoader::reset();
    observations_.clear();
}

bool AtariEnvLoader::loadFromString(const std::string& content)
{
    bool success = BaseEnvLoader::loadFromString(content);
    addObservations(getTag("OBS"));
    return success;
}

void AtariEnvLoader::loadFromEnvironment(const AtariEnv& env, const std::vector<std::vector<std::pair<std::string, std::string>>>& action_info_history /* = {} */)
{
    BaseEnvLoader::loadFromEnvironment(env, action_info_history);
    addTag("SD", std::to_string(env.getSeed()));
    int previous_lives = env.getLivesHistory()[0];
    for (size_t i = 0; i < action_pairs_.size(); ++i) {
        int lives = env.getLivesHistory()[i];
        if (lives < previous_lives) { action_pairs_[i].second["L"] = std::to_string(lives); }
        previous_lives = lives;
    }
}

std::vector<float> AtariEnvLoader::getFeatures(const int pos, utils::Rotation rotation /* = utils::Rotation::kRotationNone */) const
{
    std::vector<float> features;
    features.reserve(kAtariFeatureHistorySize * 4 * kAtariResolution * kAtariResolution);
    int start = pos - kAtariFeatureHistorySize + 1, end = pos;
    for (int i = start; i <= end; ++i) { // 1 for action; 3 for RGB, action first since the latest observation didn't have action yet
        int action_id = (i - 1 < 0 ? 0
                                   : (i - 1 >= static_cast<int>(action_pairs_.size()) ? utils::Random::randInt() % kAtariActionSize : action_pairs_[i - 1].first.getActionID()));
        assert(action_id >= 0 && action_id < kAtariActionSize);
        std::vector<float> action_features(kAtariResolution * kAtariResolution, action_id * 1.0f / kAtariActionSize);
        features.insert(features.end(), action_features.begin(), action_features.end());
        if (i >= 0) {
            const std::string& observation = (i < static_cast<int>(observations_.size()) ? observations_[i] : observations_.back());
            if (observation.empty()) { return getFeaturesByReplay(pos, rotation); }
            for (const auto& o : observation) { features.push_back(static_cast<unsigned int>(static_cast<unsigned char>(o)) / 255.0f); }
        } else {
            std::vector<float> f(3 * kAtariResolution * kAtariResolution, 0.0f);
            features.insert(features.end(), f.begin(), f.end());
        }
    }
    assert(static_cast<int>(features.size()) == kAtariFeatureHistorySize * 4 * kAtariResolution * kAtariResolution);
    return features;
}

std::vector<float> AtariEnvLoader::getActionFeatures(const int pos, utils::Rotation rotation /* = utils::Rotation::kRotationNone */) const
{
    int hidden_size = kAtariHiddenChannelHeight * kAtariHiddenChannelWidth;
    std::vector<float> action_features(kAtariActionSize * hidden_size, 0.0f);
    if (pos < static_cast<int>(action_pairs_.size())) {
        const AtariAction& action = action_pairs_[pos].first;
        std::fill(action_features.begin() + action.getActionID() * hidden_size, action_features.begin() + (action.getActionID() + 1) * hidden_size, 1.0f);
    } else {
        int action_id = utils::Random::randInt() % kAtariActionSize;
        std::fill(action_features.begin() + action_id * hidden_size, action_features.begin() + (action_id + 1) * hidden_size, 1.0f);
    }
    return action_features;
}

void AtariEnvLoader::addObservations(const std::string& compressed_obs)
{
    observations_.resize(action_pairs_.size() + 1, "");
    if (compressed_obs.empty()) { return; }

    int obs_length = 3 * kAtariResolution * kAtariResolution;
    std::string observations_str = utils::decompressString(compressed_obs);
    assert(observations_str.size() % obs_length == 0);

    int index = observations_.size();
    for (size_t end = observations_str.size(); end > 0; end -= obs_length) { observations_[--index] = observations_str.substr(end - obs_length, obs_length); }
    assert(index >= 0);
}

std::vector<float> AtariEnvLoader::getFeaturesByReplay(const int pos, utils::Rotation rotation /* = utils::Rotation::kRotationNone */) const
{
    AtariEnv env;
    env.reset(std::stoi(getTag("SD")));
    for (int i = 0; i < pos; ++i) { env.act(action_pairs_[i].first); }
    return env.getFeatures(rotation);
}

float AtariEnvLoader::calculateNStepValue(const int pos) const
{
    assert(pos < static_cast<int>(action_pairs_.size()));

    // calculate n-step return by using EpisodicLifeEnv (end-of-life == end-of-episode)
    // reference: https://github.com/DLR-RM/stable-baselines3/blob/472ff8edb815070c405da913dcbe64c1a06e0e7d/stable_baselines3/common/atari_wrappers.py#L98
    const int n_step = config::learner_n_step_return;
    const float discount = config::actor_mcts_reward_discount;
    size_t bootstrap_index = pos + n_step;
    float value = 0.0f;
    float n_step_value = ((bootstrap_index < action_pairs_.size() && !action_pairs_[bootstrap_index].second.count("L")) ? std::pow(discount, n_step) * BaseEnvLoader::getValue(bootstrap_index)[0] : 0.0f);
    for (size_t index = pos; index < std::min(bootstrap_index, action_pairs_.size()); ++index) {
        if (action_pairs_[index].second.count("L") && std::stoi(action_pairs_[index].second.at("L")) > 0) { return value; }
        float reward = BaseEnvLoader::getReward(index)[0];
        value += std::pow(discount, index - pos) * reward;
    }
    value += n_step_value;
    return value;
}

std::vector<float> AtariEnvLoader::toDiscreteValue(float value) const
{
    std::vector<float> discrete_value(kAtariDiscreteValueSize, 0.0f);
    int value_floor = floor(value);
    int value_ceil = ceil(value);
    int shift = kAtariDiscreteValueSize / 2;
    int value_floor_shift = std::min(std::max(value_floor + shift, 0), kAtariDiscreteValueSize - 1);
    int value_ceil_shift = std::min(std::max(value_ceil + shift, 0), kAtariDiscreteValueSize - 1);
    if (value_floor == value_ceil) {
        discrete_value[value_floor_shift] = 1.0f;
    } else {
        discrete_value[value_floor_shift] = value_ceil - value;
        discrete_value[value_ceil_shift] = value - value_floor;
    }
    return discrete_value;
}

} // namespace minizero::env::atari
