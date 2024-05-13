#include "data_loader.h"
#include "configuration.h"
#include "environment.h"
#include "random.h"
#include "rotation.h"
#include <algorithm>
#include <fstream>
#include <utility>

namespace minizero::learner {

using namespace minizero;
using namespace minizero::utils;

ReplayBuffer::ReplayBuffer()
{
    num_data_ = 0;
    game_priority_sum_ = 0.0f;
    game_priorities_.clear();
    position_priorities_.clear();
    env_loaders_.clear();
}

void ReplayBuffer::addData(const EnvironmentLoader& env_loader)
{
    std::pair<int, int> data_range = env_loader.getDataRange();
    std::deque<float> position_priorities(data_range.second + 1, 0.0f);
    float game_priority = 0.0f;
    for (int i = data_range.first; i <= data_range.second; ++i) {
        position_priorities[i] = std::pow((config::learner_use_per ? env_loader.getPriority(i) : 1.0f), config::learner_per_alpha);
        game_priority += position_priorities[i];
    }

    std::lock_guard<std::mutex> lock(mutex_);

    // add new data to replay buffer
    num_data_ += (data_range.second - data_range.first + 1);
    position_priorities_.push_back(position_priorities);
    game_priorities_.push_back(game_priority);
    env_loaders_.push_back(env_loader);

    // remove old data if replay buffer is full
    const size_t replay_buffer_max_size = config::zero_replay_buffer * config::zero_num_games_per_iteration;
    while (position_priorities_.size() > replay_buffer_max_size) {
        data_range = env_loaders_.front().getDataRange();
        num_data_ -= (data_range.second - data_range.first + 1);
        position_priorities_.pop_front();
        game_priorities_.pop_front();
        env_loaders_.pop_front();
    }
}

std::pair<int, int> ReplayBuffer::sampleEnvAndPos()
{
    int env_id = sampleIndex(game_priorities_);
    int pos_id = sampleIndex(position_priorities_[env_id]);
    return {env_id, pos_id};
}

int ReplayBuffer::sampleIndex(const std::deque<float>& weight)
{
    std::discrete_distribution<> dis(weight.begin(), weight.end());
    return dis(Random::generator_);
}

float ReplayBuffer::getLossScale(const std::pair<int, int>& p)
{
    if (!config::learner_use_per) { return 1.0f; }

    // calculate importance sampling ratio
    int env_id = p.first, pos = p.second;
    float prob = position_priorities_[env_id][pos] / game_priority_sum_;
    return std::pow((num_data_ * prob), (-config::learner_per_init_beta));
}

std::string DataLoaderSharedData::getNextEnvString()
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::string env_string = "";
    if (!env_strings_.empty()) {
        env_string = env_strings_.front();
        env_strings_.pop_front();
    }
    return env_string;
}

int DataLoaderSharedData::getNextBatchIndex()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return (batch_index_ < config::learner_batch_size ? batch_index_++ : config::learner_batch_size);
}

void DataLoaderThread::initialize()
{
    int seed = config::program_auto_seed ? std::random_device()() : config::program_seed + id_;
    Random::seed(seed);
}

void DataLoaderThread::runJob()
{
    if (!getSharedData()->env_strings_.empty()) {
        while (addEnvironmentLoader()) {}
    } else {
        while (sampleData()) {}
    }
}

bool DataLoaderThread::addEnvironmentLoader()
{
    std::string env_string = getSharedData()->getNextEnvString();
    if (env_string.empty()) { return false; }

    EnvironmentLoader env_loader;
    if (env_loader.loadFromString(env_string)) { getSharedData()->replay_buffer_.addData(env_loader); }
    return true;
}

bool DataLoaderThread::sampleData()
{
    int batch_index = getSharedData()->getNextBatchIndex();
    if (batch_index >= config::learner_batch_size) { return false; }

    if (config::nn_type_name == "alphazero") {
        setAlphaZeroTrainingData(batch_index);
    } else if (config::nn_type_name == "muzero") {
        setMuZeroTrainingData(batch_index);
    } else {
        return false; // should not be here
    }

    return true;
}

void DataLoaderThread::setAlphaZeroTrainingData(int batch_index)
{
    // random pickup one position
    std::pair<int, int> p = getSharedData()->replay_buffer_.sampleEnvAndPos();
    int env_id = p.first, pos = p.second;

    // AlphaZero training data
    const EnvironmentLoader& env_loader = getSharedData()->replay_buffer_.env_loaders_[env_id];
    Rotation rotation = static_cast<Rotation>(Random::randInt() % static_cast<int>(Rotation::kRotateSize));
    float loss_scale = getSharedData()->replay_buffer_.getLossScale(p);
    std::vector<float> features = env_loader.getFeatures(pos, rotation);
    std::vector<float> policy = env_loader.getPolicy(pos, rotation);
    std::vector<float> value = env_loader.getValue(pos);

    // write data to data_ptr
    getSharedData()->getDataPtr()->loss_scale_[batch_index] = loss_scale;
    getSharedData()->getDataPtr()->sampled_index_[2 * batch_index] = p.first;
    getSharedData()->getDataPtr()->sampled_index_[2 * batch_index + 1] = p.second;
    std::copy(features.begin(), features.end(), getSharedData()->getDataPtr()->features_ + features.size() * batch_index);
    std::copy(policy.begin(), policy.end(), getSharedData()->getDataPtr()->policy_ + policy.size() * batch_index);
    std::copy(value.begin(), value.end(), getSharedData()->getDataPtr()->value_ + value.size() * batch_index);
}

void DataLoaderThread::setMuZeroTrainingData(int batch_index)
{
    // random pickup one position
    std::pair<int, int> p = getSharedData()->replay_buffer_.sampleEnvAndPos();
    int env_id = p.first, pos = p.second;

    // MuZero training data
    const EnvironmentLoader& env_loader = getSharedData()->replay_buffer_.env_loaders_[env_id];
    Rotation rotation = static_cast<Rotation>(Random::randInt() % static_cast<int>(Rotation::kRotateSize));
    float loss_scale = getSharedData()->replay_buffer_.getLossScale(p);
    std::vector<float> features = env_loader.getFeatures(pos, rotation);
    std::vector<float> action_features, policy, value, reward, tmp;
    for (int step = 0; step <= config::learner_muzero_unrolling_step; ++step) {
        // action features
        if (step < config::learner_muzero_unrolling_step) {
            tmp = env_loader.getActionFeatures(pos + step, rotation);
            action_features.insert(action_features.end(), tmp.begin(), tmp.end());
        }

        // policy
        tmp = env_loader.getPolicy(pos + step, rotation);
        policy.insert(policy.end(), tmp.begin(), tmp.end());

        // value
        tmp = env_loader.getValue(pos + step);
        value.insert(value.end(), tmp.begin(), tmp.end());

        // reward
        if (step < config::learner_muzero_unrolling_step) {
            tmp = env_loader.getReward(pos + step);
            reward.insert(reward.end(), tmp.begin(), tmp.end());
        }
    }

    // write data to data_ptr
    getSharedData()->getDataPtr()->loss_scale_[batch_index] = loss_scale;
    getSharedData()->getDataPtr()->sampled_index_[2 * batch_index] = p.first;
    getSharedData()->getDataPtr()->sampled_index_[2 * batch_index + 1] = p.second;
    std::copy(features.begin(), features.end(), getSharedData()->getDataPtr()->features_ + features.size() * batch_index);
    std::copy(action_features.begin(), action_features.end(), getSharedData()->getDataPtr()->action_features_ + action_features.size() * batch_index);
    std::copy(policy.begin(), policy.end(), getSharedData()->getDataPtr()->policy_ + policy.size() * batch_index);
    std::copy(value.begin(), value.end(), getSharedData()->getDataPtr()->value_ + value.size() * batch_index);
    std::copy(reward.begin(), reward.end(), getSharedData()->getDataPtr()->reward_ + reward.size() * batch_index);
}

DataLoader::DataLoader(const std::string& conf_file_name)
{
    env::setUpEnv();
    config::ConfigureLoader cl;
    config::setConfiguration(cl);
    cl.loadFromFile(conf_file_name);
}

void DataLoader::initialize()
{
    createSlaveThreads(config::learner_num_thread);
    getSharedData()->createDataPtr();
}

void DataLoader::loadDataFromFile(const std::string& file_name)
{
    std::ifstream fin(file_name, std::ifstream::in);
    for (std::string content; std::getline(fin, content);) { getSharedData()->env_strings_.push_back(content); }

    for (auto& t : slave_threads_) { t->start(); }
    for (auto& t : slave_threads_) { t->finish(); }
    getSharedData()->replay_buffer_.game_priority_sum_ = std::accumulate(getSharedData()->replay_buffer_.game_priorities_.begin(), getSharedData()->replay_buffer_.game_priorities_.end(), 0.0f);
}

void DataLoader::sampleData()
{
    getSharedData()->batch_index_ = 0;
    for (auto& t : slave_threads_) { t->start(); }
    for (auto& t : slave_threads_) { t->finish(); }
}

void DataLoader::updatePriority(int* sampled_index, float* batch_values)
{
    // TODO: use multiple threads
    for (int batch_index = 0; batch_index < config::learner_batch_size; ++batch_index) {
        int env_id = sampled_index[2 * batch_index];
        int pos_id = sampled_index[2 * batch_index + 1];

        EnvironmentLoader& env_loader = getSharedData()->replay_buffer_.env_loaders_[env_id];
        for (int step = 0; step <= config::learner_muzero_unrolling_step; ++step) {
            float new_value = utils::invertValue(batch_values[step * config::learner_batch_size + batch_index]);
            env_loader.setActionPairInfo(pos_id + step, "V", std::to_string(new_value));
        }
        getSharedData()->replay_buffer_.position_priorities_[env_id][pos_id] = std::pow(env_loader.getPriority(pos_id), config::learner_per_alpha);
    }

    // recalculate priority to correct floating number error (TODO: speedup this)
    for (size_t i = 0; i < getSharedData()->replay_buffer_.game_priorities_.size(); ++i) {
        getSharedData()->replay_buffer_.game_priorities_[i] = std::accumulate(getSharedData()->replay_buffer_.position_priorities_[i].begin(), getSharedData()->replay_buffer_.position_priorities_[i].end(), 0.0f);
    }
    getSharedData()->replay_buffer_.game_priority_sum_ = std::accumulate(getSharedData()->replay_buffer_.game_priorities_.begin(), getSharedData()->replay_buffer_.game_priorities_.end(), 0.0f);
}

} // namespace minizero::learner
