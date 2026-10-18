#!/usr/bin/env python3
"""Generate benchmark.db with 100K rows for the /db endpoint.

Expands the 50-item dataset.json into 100,000 rows with varied prices.
The /db endpoint queries: SELECT ... WHERE price BETWEEN ? AND ? LIMIT 50
with NO index on price, forcing a full table scan every request.
"""
import json, sqlite3, sys, os, random, hashlib

script_dir = os.path.dirname(os.path.abspath(__file__))
root_dir = os.path.dirname(script_dir)

data_file = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root_dir, "data/dataset.json")
db_file = sys.argv[2] if len(sys.argv) > 2 else os.path.join(root_dir, "data/benchmark.db")

seed_data = json.load(open(data_file))
TARGET_ROWS = 100_000

if os.path.exists(db_file):
    os.remove(db_file)

conn = sqlite3.connect(db_file)
conn.execute("PRAGMA journal_mode=DELETE")
conn.execute("PRAGMA page_size=4096")
conn.execute("""
    CREATE TABLE items (
        id INTEGER PRIMARY KEY,
        name TEXT NOT NULL,
        category TEXT NOT NULL,
        price REAL NOT NULL,
        quantity INTEGER NOT NULL,
        active INTEGER NOT NULL,
        tags TEXT NOT NULL,
        rating_score REAL NOT NULL,
        rating_count INTEGER NOT NULL
    )
""")
# NO index on price — forces table scan

rng = random.Random(42)  # deterministic seed
categories = [d["category"] for d in seed_data]
names = [d["name"] for d in seed_data]
all_tags = []
for d in seed_data:
    all_tags.extend(d["tags"])
all_tags = list(set(all_tags))

rows = []
for i in range(1, TARGET_ROWS + 1):
    base = seed_data[(i - 1) % len(seed_data)]
    price = round(rng.uniform(1.0, 500.0), 2)
    quantity = rng.randint(1, 1000)
    active = rng.choice([0, 1])
    ntags = rng.randint(1, 4)
    tags = json.dumps(rng.sample(all_tags, min(ntags, len(all_tags))))
    name = f"{rng.choice(names)} {i}"
    category = rng.choice(categories)
    rating_score = round(rng.uniform(1.0, 5.0), 1)
    rating_count = rng.randint(1, 500)
    rows.append((i, name, category, price, quantity, active, tags, rating_score, rating_count))

conn.executemany("INSERT INTO items VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)", rows)
conn.commit()

# Verify
count = conn.execute("SELECT COUNT(*) FROM items").fetchone()[0]
size = os.path.getsize(db_file)
conn.close()
print(f"Created {db_file}: {count} rows, {size / 1024 / 1024:.1f} MB")
