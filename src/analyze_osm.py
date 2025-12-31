#!/usr/bin/env python3

import xml.etree.ElementTree as ET
from collections import defaultdict, Counter
import os

def get_highway_attributes(file_path):
    try:
        tree = ET.parse(file_path)
        root = tree.getroot()
    except Exception as e:
        return f"Error parsing XML: {e}"

    # Dictionary to store unique combinations
    # Key: highway value, Value: set of (width, surface) tuples
    highway_data = defaultdict(set)
    highway_counts = defaultdict(int)

    # Iterate through all elements that have a 'highway' tag
    for element in root.findall(".//*[@k='highway']/.."):
        tags = {tag.get('k'): tag.get('v') for tag in element.findall('tag')}
        
        h_val = tags.get('highway')
        w_val = tags.get('width', 'N/A')
        s_val = tags.get('surface', 'N/A')
        
        highway_counts[h_val] += 1
        highway_data[h_val].add((w_val, s_val))

    # Display results
    print(f"{'Highway Value':<20} | {'Count':<8} | {'Width':<10} | {'Surface':<15}")
    print("-" * 60)
    total_highways = 0
    for h_type in sorted(highway_data.keys()):
        count = highway_counts[h_type]
        total_highways += count
        for width, surface in sorted(highway_data[h_type]):
            print(f"{h_type:<20} | {count:<8} | {width:<10} | {surface:<15}")

    print(f"Total number of highway types: {len(highway_data)}")
    print(f"Total number of highways: {total_highways}")

def analyze_osm_relations(file_path):
    """
    Parses an OSM file and analyzes tags within <relation> elements.
    """
    if not os.path.exists(file_path):
        print(f"File not found: {file_path}")
        return

    tag_key_counter = Counter()
    tag_kv_counter = Counter()
    relation_count = 0

    try:
        # Use iterparse to handle potentially large OSM files without loading everything into memory
        for event, elem in ET.iterparse(file_path, events=('end',)):
            if elem.tag == 'relation':
                relation_count += 1
                for tag in elem.findall('tag'):
                    k = tag.get('k')
                    v = tag.get('v')
                    if k:
                        tag_key_counter[k] += 1
                        if v:
                            tag_kv_counter[f"{k}={v}"] += 1
                elem.clear() # Free memory
        
        if relation_count == 0:
            print("No relation elements found in the file.")
            return

        print(f"Analyzed {relation_count} relations.\n")
        print("--- Most Common Tag Keys ---")
        for k, count in tag_key_counter.most_common(5):
            print(f"{k}: {count}")
            
        print("\n--- Most Common Tag Types (Key=Value) ---")
        for kv, count in tag_kv_counter.most_common(10):
            print(f"{kv}: {count}")

    except ET.ParseError as e:
        print(f"XML Parse Error: {e}")

if __name__ == "__main__":
    # get_highway_attributes('/home/benjamin/Downloads/map.osm')
    analyze_osm_relations('/home/benjamin/Downloads/map.osm')

