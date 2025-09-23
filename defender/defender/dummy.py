import pickle

# Simple dummy model
class DummyModel:
    def predict(self, bytez: bytes) -> int:
        return 1  # always returns "malware"; change to 0 for benign

# Save it as a pickle
with open("nfs_full.pickle", "wb") as f:
    pickle.dump(DummyModel(), f)

print("Dummy model saved as nfs_full.pickle")
