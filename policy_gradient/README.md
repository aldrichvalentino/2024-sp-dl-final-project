# Policy Gradient for Atari Enduro

This folder contains the code for training and testing vanilla policy gradient in playing Enduro, Atari 2600 racing game.

# How to Run

1. If you want to re-train the model, execute the `vpg_enduro.ipynb` file. This will save the best model to `policy_pi.pth`.
2. If you want to use the pre-trained model, execute the `demo.ipynb` file. This will import the model checkpoint, play a single game, and display the rewards and estimated in-game score.

# Disclaimer

The model was trained using Google Colab T4 Python environment on May 2024. Due to Torch RL and OpenAI Gym deprecation, you may encounter errors when running this code in the future.
