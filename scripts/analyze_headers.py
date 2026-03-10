#!/usr/bin/env python3
"""
Analyze header file usage in AstraDB project
Used for optimizing precompiled header files (pch.h)
"""
import os
import re
from collections import Counter
from pathlib import Path

def extract_includes(file_path):
    """Extract all #include statements from source files"""
    includes = []
    try:
        with open(file_path, 'r', encoding='utf-8') as f:
            content = f.read()
            # Match #include "..." and #include <...>
            matches = re.findall(r'^\s*#include\s+[<"]([^>"]+)[>"]', content, re.MULTILINE)
            includes.extend(matches)
    except Exception as e:
        print(f"Error reading {file_path}: {e}")
    return includes

def analyze_project(root_dir):
    """Analyze header file usage across the entire project"""
    # Collect all source files
    source_files = []
    for pattern in ['**/*.cpp', '**/*.hpp', '**/*.h']:
        source_files.extend(Path(root_dir).rglob(pattern))
    
    print(f"Found {len(source_files)} source files")
    
    # Count header file usage
    header_counter = Counter()
    file_headers = {}
    
    for file_path in source_files:
        includes = extract_includes(file_path)
        file_headers[file_path] = includes
        header_counter.update(includes)
    
    return header_counter, file_headers

def check_header_exists(header_path, include_dirs, project_root):
    """Check if a header file exists"""
    # System headers (<...>)
    if header_path.startswith('<'):
        return True  # Assume system headers exist
    
    # Project headers ("...")
    header_path = header_path.strip('"')
    
    # Check paths relative to include directories
    for inc_dir in include_dirs:
        full_path = Path(inc_dir) / header_path
        if full_path.exists():
            return True
    
    # Check absolute path
    full_path = Path(project_root) / header_path
    if full_path.exists():
        return True
    
    return False

def main():
    root_dir = Path(__file__).parent.parent
    print(f"Analyzing project: {root_dir}")
    print("=" * 60)
    
    # Analyze header usage
    header_counter, file_headers = analyze_project(root_dir)
    
    print(f"\nTotal {len(header_counter)} unique header files used")
    print("=" * 60)
    
    # Sort by frequency
    print("\nTop 20 most used header files:")
    print("-" * 60)
    for header, count in header_counter.most_common(20):
        print(f"{count:4d}  {header}")
    
    # Project header statistics
    print("\n" + "=" * 60)
    print("Project header file usage frequency:")
    print("-" * 60)
    project_headers = [h for h in header_counter if h.startswith('"')]
    for header, count in sorted(project_headers, key=lambda x: -header_counter[x]):
        print(f"{count:4d}  {header}")
    
    # Check headers in pch.h
    print("\n" + "=" * 60)
    print("Checking headers in current pch.h:")
    print("-" * 60)
    
    pch_path = root_dir / "src" / "pch.h"
    if pch_path.exists():
        pch_includes = extract_includes(pch_path)
        print(f"\npch.h includes {len(pch_includes)} header files:")
        
        include_dirs = [
            str(root_dir / "src"),
            str(root_dir / ".cpm-cache"),
        ]
        
        missing_headers = []
        existing_headers = []
        
        for header in pch_includes:
            if check_header_exists(header, include_dirs, root_dir):
                existing_headers.append(header)
            else:
                missing_headers.append(header)
        
        if existing_headers:
            print("\n✅ Existing headers:")
            for h in existing_headers:
                count = header_counter.get(h, 0)
                print(f"   ✓ {h} (used {count} times)")
        
        if missing_headers:
            print("\n❌ Missing headers:")
            for h in missing_headers:
                print(f"   ✗ {h}")
    
    # Suggest high-frequency headers for pch.h
    print("\n" + "=" * 60)
    print("Suggested high-frequency headers for pch.h (>10 uses):")
    print("-" * 60)
    
    # Headers already in pch.h
    if pch_path.exists():
        pch_includes = extract_includes(pch_path)
        pch_includes_set = set(h.strip('"').strip('<').strip('>') for h in pch_includes)
    else:
        pch_includes_set = set()
    
    high_frequency_headers = []
    for header, count in header_counter.items():
        if count >= 10 and header not in pch_includes_set:
            # Exclude system headers (usually in standard library)
            if not header.startswith('<'):
                high_frequency_headers.append((header, count))
    
    high_frequency_headers.sort(key=lambda x: -x[1])
    for header, count in high_frequency_headers[:30]:
        print(f"{count:4d}  {header}")
    
    # Standard library header statistics
    print("\n" + "=" * 60)
    print("High-frequency standard library headers (>5 uses):")
    print("-" * 60)
    std_headers = {h: count for h, count in header_counter.items() if h.startswith('<')}
    std_headers = [(h.strip('<').strip('>'), count) for h, count in std_headers.items()]
    std_headers.sort(key=lambda x: -x[1])
    
    for header, count in std_headers[:20]:
        print(f"{count:4d}  <{header}>")

if __name__ == "__main__":
    main()