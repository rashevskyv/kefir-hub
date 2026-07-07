import os
import json

i18n_dir = r"d:\git\dev\sphaira\assets\romfs\i18n"
en_path = os.path.join(i18n_dir, "en.json")
with open(en_path, 'r', encoding='utf-8') as f:
    en_data = json.load(f)

filepath = os.path.join(i18n_dir, "uk.json")
with open(filepath, 'r', encoding='utf-8') as f:
    data = json.load(f)

missing = []
for key in en_data:
    if (key not in data) or (data[key] == "") or (data[key] == key and key not in ["DBI", "FTP", "MTP", "Nxlink", "GitHub", "IRS", "de", "es", "fr", "it", "ja", "ko", "nl", "pt", "se", "vi", "zh"]):
        missing.append((key, data.get(key, "NOT IN FILE")))

print("Missing in uk.json:")
for m in missing:
    print(f"Key: {m[0]} | Value: {m[1]}")
