import json
import pandas as pd
import pickle
from train.train_classifier import JSONAttributeExtractor, NeedForSpeedModel  # adjust import if needed

# path to your saved model
MODEL_PATH = './defender/defender/models/nfs_libraries_functions_nostrings.pickle'

# test files you have
test_files = [
    "./train/ember2018/train_features_2.jsonl"  # add more if you have them
]

# load the trained model
with open(MODEL_PATH, 'rb') as f:
    clf = pickle.load(f)

test_attributes_list = []

# read test data
for file_path in test_files:
    print(f"Reading {file_path}...")
    with open(file_path, 'r') as f:
        lines = f.readlines()
        for line in lines:
            extractor = JSONAttributeExtractor(line)
            test_attributes_list.append(extractor.extract())

# convert to DataFrame
test_data = pd.DataFrame(test_attributes_list)
test_data = test_data[(test_data["label"]==0) | (test_data["label"]==1)]

# save labels separately
test_labels = test_data["label"].values

# predict with the model
predictions = clf.predict(test_data)
probabilities = clf.predict_proba(test_data)

# evaluate
from sklearn.metrics import accuracy_score, f1_score, recall_score, precision_score, confusion_matrix

acc = accuracy_score(test_labels, predictions)
rec = recall_score(test_labels, predictions)
pre = precision_score(test_labels, predictions)
f1 = f1_score(test_labels, predictions)

tn, fp, fn, tp = confusion_matrix(test_labels, predictions).ravel()
fpr = fp / (fp + tn)
fnr = fn / (tp + fn)

print(f"Accuracy: {acc}")
print(f"Precision: {pre}")
print(f"Recall: {rec}")
print(f"F1-score: {f1}")
print(f"False Positive Rate: {fpr}")
print(f"False Negative Rate: {fnr}")
