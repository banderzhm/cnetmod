#!/usr/bin/env python3
"""Generate the dataset.json file for the /process benchmark."""
import json
import math

CATEGORIES = ["electronics", "tools", "clothing", "food", "sports", "books", "toys", "health"]
ADJECTIVES = ["Alpha", "Pro", "Ultra", "Nano", "Prime", "Core", "Flex", "Edge", "Swift", "Nova"]
NOUNS = ["Widget", "Gadget", "Module", "Sensor", "Driver", "Relay", "Switch", "Valve", "Gear", "Link"]
TAG_POOL = ["fast", "new", "popular", "sale", "limited", "premium", "eco", "compact", "heavy-duty", "wireless"]

items = []
for i in range(50):
    adj = ADJECTIVES[i % len(ADJECTIVES)]
    noun = NOUNS[(i * 7) % len(NOUNS)]
    cat = CATEGORIES[i % len(CATEGORIES)]
    price = round(5.0 + (i * 73 % 997) / 10.0, 2)
    qty = 1 + (i * 31) % 20
    score = round(1.0 + (i * 47 % 41) / 10.0, 1)
    review_count = 10 + (i * 53) % 500
    active = (i % 3 != 0)
    tags = [TAG_POOL[(i + j) % len(TAG_POOL)] for j in range(1 + i % 3)]

    items.append({
        "id": i + 1,
        "name": f"{adj} {noun}",
        "category": cat,
        "price": price,
        "quantity": qty,
        "active": active,
        "tags": tags,
        "rating": {
            "score": score,
            "count": review_count
        }
    })

with open("data/dataset.json", "w") as f:
    json.dump(items, f, indent=2)

print(f"Generated data/dataset.json with {len(items)} items")

# Also compute and print expected response for validation
response_items = []
for item in items:
    out = dict(item)
    out["total"] = round(item["price"] * item["quantity"], 2)
    response_items.append(out)

response = {"items": response_items, "count": len(response_items)}
checksum = sum(item["total"] for item in response_items)
print(f"Expected checksum (sum of totals): {checksum}")
