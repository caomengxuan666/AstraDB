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
        with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
            content = f.read()
            # Match #include "..." and #include <...> - preserve the full include syntax
            matches = re.findall(r'^\s*#include\s+([<"][^>"]+[>"])', content, re.MULTILINE)
            includes.extend(matches)
    except Exception as e:
        print(f"Error reading {file_path}: {e}")
    return includes

def analyze_project(root_dir):
    """Analyze header file usage across the entire project"""
    # Collect all source files
    source_files = []
    # Exclude cache and build directories
    exclude_dirs = {'.git', 'build', 'cmake-build-*', '.cpm-cache'}
    
    for pattern in ['**/*.cpp', '**/*.hpp', '**/*.h', '**/*.c', '**/*.cc']:
        for file_path in Path(root_dir).rglob(pattern):
            # Skip excluded directories
            if any(excluded in str(file_path) for excluded in exclude_dirs):
                continue
            source_files.append(file_path)
    
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
    # Standard C++ library headers
    std_cpp_headers = {
        'algorithm', 'array', 'atomic', 'bitset', 'chrono', 'codecvt',
        'complex', 'condition_variable', 'deque', 'exception', 'filesystem',
        'forward_list', 'fstream', 'functional', 'future', 'initializer_list',
        'iomanip', 'ios', 'iosfwd', 'iostream', 'istream', 'iterator',
        'limits', 'list', 'locale', 'map', 'memory', 'mutex', 'new',
        'numeric', 'optional', 'ostream', 'queue', 'random', 'ranges',
        'ratio', 'regex', 'scoped_allocator', 'set', 'shared_mutex',
        'sstream', 'stack', 'stdexcept', 'streambuf', 'string', 'string_view',
        'strstream', 'system_error', 'thread', 'tuple', 'type_traits',
        'typeindex', 'typeinfo', 'unordered_map', 'unordered_set', 'utility',
        'valarray', 'variant', 'vector', 'version'
    }
    
    # Standard C library headers
    std_c_headers = {
        'assert.h', 'complex.h', 'ctype.h', 'errno.h', 'fenv.h', 'float.h',
        'inttypes.h', 'iso646.h', 'limits.h', 'locale.h', 'math.h', 'setjmp.h',
        'signal.h', 'stdalign.h', 'stdarg.h', 'stdatomic.h', 'stdbool.h',
        'stddef.h', 'stdint.h', 'stdio.h', 'stdlib.h', 'stdnoreturn.h',
        'string.h', 'tgmath.h', 'threads.h', 'time.h', 'uchar.h', 'wchar.h',
        'wctype.h'
    }
    
    # Extract clean path
    if header_path.startswith('<') or header_path.startswith('"'):
        clean_path = header_path.strip('"').strip('<').strip('>')
    else:
        clean_path = header_path
    
    # Check if it's a standard library header
    if header_path.startswith('<'):
        if clean_path in std_cpp_headers or clean_path in std_c_headers:
            return True
        # Common C++ standard library headers without extension
        if clean_path in [h.replace('.h', '') for h in std_c_headers]:
            return True
    
    # Check include directories
    for inc_dir in include_dirs:
        # Try different path variations
        possible_paths = [
            Path(inc_dir) / clean_path,
            Path(inc_dir) / clean_path.replace('.hpp', '.h'),
            Path(inc_dir) / clean_path.replace('.h', '.hpp'),
            Path(inc_dir) / 'include' / clean_path,
            Path(inc_dir) / 'src' / clean_path,
            Path(inc_dir) / 'includes' / clean_path,
        ]
        
        # Add paths for third-party libraries
        if 'absl/' in clean_path or 'asio/' in clean_path or 'fmt/' in clean_path:
            # Try without the top-level directory
            sub_path = '/'.join(clean_path.split('/')[1:])
            possible_paths.append(Path(inc_dir) / sub_path)
        
        for path in possible_paths:
            if path.exists():
                return True
    
    # Check relative to project root
    full_path = Path(project_root) / clean_path
    if full_path.exists():
        return True
    
    # Check in system include paths
    system_paths = [
        '/usr/include',
        '/usr/local/include',
        '/usr/include/c++/11',
        '/usr/include/x86_64-linux-gnu',
        '/usr/lib/gcc/x86_64-linux-gnu/11/include',
    ]
    
    for sys_path in system_paths:
        sys_full = Path(sys_path) / clean_path
        if sys_full.exists():
            return True
    
    return False

def find_include_directories(root_dir):
    """Find all possible include directories in the project"""
    include_dirs = [str(root_dir / 'src')]
    
    # Common include directory names
    include_names = ['include', 'includes', 'inc', 'headers', 'public', 'src']
    
    # Search for include directories
    for pattern in ['**/include', '**/includes', '**/inc', '**/src', '**/public']:
        for path in root_dir.glob(pattern):
            if path.is_dir() and str(path) not in include_dirs:
                include_dirs.append(str(path))
    
    # Add CPM cache directories - recursively find all source/include directories
    cpm_cache = root_dir / '.cpm-cache'
    if cpm_cache.exists():
        for pkg_dir in cpm_cache.iterdir():
            if pkg_dir.is_dir():
                # Look for hash-named subdirectories (like 2e8f in abseil)
                for subdir in pkg_dir.iterdir():
                    if subdir.is_dir() and len(subdir.name) <= 8:  # Hash directories are usually short
                        include_dirs.append(str(subdir))
                        
                        # Look for include directories inside the hash directory
                        for inc_name in include_names:
                            inc_path = subdir / inc_name
                            if inc_path.exists() and inc_path.is_dir():
                                include_dirs.append(str(inc_path))
                        
                        # Also look for abseil-cpp specific structure
                        if pkg_dir.name == 'abseil':
                            abseil_cpp = subdir / 'abseil-cpp'
                            if abseil_cpp.exists():
                                include_dirs.append(str(abseil_cpp))
                                # abseil headers are in abseil-cpp/absl/
                                absl_dir = abseil_cpp / 'absl'
                                if absl_dir.exists():
                                    include_dirs.append(str(absl_dir))
    
    # Add third-party directories
    for third_party in ['third_party', 'thirdparty', 'external', 'deps']:
        tp_path = root_dir / third_party
        if tp_path.exists():
            include_dirs.append(str(tp_path))
            for inc_name in include_names:
                inc_path = tp_path / inc_name
                if inc_path.exists() and inc_path.is_dir():
                    include_dirs.append(str(inc_path))
    
    # Remove duplicates and return
    return list(set(include_dirs))

def normalize_header_name(header):
    """Normalize header name for comparison"""
    # Remove quotes and brackets but keep the format for display
    if header.startswith('<'):
        return header.strip('<>')
    elif header.startswith('"'):
        return header.strip('"')
    return header

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
    print("Project header file usage frequency (quoted includes):")
    print("-" * 60)
    project_headers = [(h, count) for h, count in header_counter.items() 
                      if h.startswith('"') and not h.startswith('"astra/')]
    for header, count in sorted(project_headers, key=lambda x: -x[1])[:30]:
        print(f"{count:4d}  {header}")
    
    # Check headers in pch.h
    print("\n" + "=" * 60)
    print("Checking headers in current pch.h:")
    print("-" * 60)
    
    pch_path = root_dir / "src" / "pch.h"
    if pch_path.exists():
        pch_includes = extract_includes(pch_path)
        print(f"\npch.h includes {len(pch_includes)} header files:")
        
        # Find all include directories
        include_dirs = find_include_directories(root_dir)
        print(f"Checking against {len(include_dirs)} include directories")
        
        missing_headers = []
        existing_headers = []
        
        # Standard C++ headers that should always exist
        std_headers = {
            'algorithm', 'array', 'atomic', 'cassert', 'chrono', 'cmath',
            'cstddef', 'cstdint', 'cstdio', 'cstdlib', 'cstring', 'ctime',
            'exception', 'functional', 'iomanip', 'iostream', 'iterator',
            'limits', 'memory', 'mutex', 'map', 'new', 'numeric', 'optional',
            'ostream', 'queue', 'set', 'sstream', 'stack', 'stdexcept',
            'string', 'string_view', 'thread', 'tuple', 'type_traits',
            'unordered_map', 'unordered_set', 'utility', 'vector'
        }
        
        for header in pch_includes:
            # Extract the header name without brackets/quotes for checking
            clean_name = header.strip('<>').strip('"')
            
            # Check if it's a standard C++ header
            if header.startswith('<') and clean_name in std_headers:
                existing_headers.append(header)
                continue
            
            # Check if file exists
            if check_header_exists(header, include_dirs, root_dir):
                existing_headers.append(header)
            else:
                missing_headers.append(header)
        
        if existing_headers:
            print("\n✅ Existing headers:")
            # Group by type for better readability
            std_headers_found = [h for h in existing_headers if h.startswith('<')]
            project_headers_found = [h for h in existing_headers if h.startswith('"')]
            
            if std_headers_found:
                print("\n   Standard Library Headers:")
                # Sort standard headers alphabetically
                for h in sorted(std_headers_found, key=lambda x: x.strip('<>')):
                    count = header_counter.get(h, 0)
                    print(f"     ✓ {h} (used {count} times)")
            
            if project_headers_found:
                print("\n   Project/Third-party Headers:")
                for h in sorted(project_headers_found):
                    count = header_counter.get(h, 0)
                    print(f"     ✓ {h} (used {count} times)")
        
        if missing_headers:
            print("\n❌ Missing headers (may need to be added to include paths):")
            # Sort missing headers by type
            missing_std = [h for h in missing_headers if h.startswith('<')]
            missing_proj = [h for h in missing_headers if h.startswith('"')]
            
            if missing_std:
                print("\n   Standard Library Headers (should exist):")
                for h in sorted(missing_std, key=lambda x: x.strip('<>')):
                    print(f"     ✗ {h}")
            
            if missing_proj:
                print("\n   Project/Third-party Headers:")
                for h in sorted(missing_proj):
                    print(f"     ✗ {h}")
    
    # Suggest high-frequency headers for pch.h
    print("\n" + "=" * 60)
    print("Suggested high-frequency headers for pch.h (>10 uses):")
    print("-" * 60)
    
    # Headers already in pch.h
    if pch_path.exists():
        pch_includes = extract_includes(pch_path)
        pch_includes_set = set(pch_includes)
    else:
        pch_includes_set = set()
    
    # High frequency headers not in pch.h
    high_frequency_headers = []
    for header, count in header_counter.items():
        if count >= 10 and header not in pch_includes_set:
            # Exclude some ASIO internal headers that might be problematic
            if not any(x in header for x in ['push_options', 'pop_options']):
                high_frequency_headers.append((header, count))
    
    high_frequency_headers.sort(key=lambda x: -x[1])
    for header, count in high_frequency_headers[:30]:
        # Clean up display
        display_header = header
        if len(display_header) > 50:
            display_header = display_header[:47] + "..."
        print(f"{count:4d}  {display_header}")
    
    # Standard library header statistics
    print("\n" + "=" * 60)
    print("Standard library header usage frequency:")
    print("-" * 60)
    
    # Count standard headers (with < >)
    std_headers_count = [(h, count) for h, count in header_counter.items() 
                        if h.startswith('<') and count >= 5]
    std_headers_count.sort(key=lambda x: -x[1])
    
    for header, count in std_headers_count[:30]:
        print(f"{count:4d}  {header}")
    
    # Summary statistics
    print("\n" + "=" * 60)
    print("SUMMARY")
    print("=" * 60)
    
    total_includes = sum(header_counter.values())
    print(f"Total include statements: {total_includes}")
    print(f"Unique headers: {len(header_counter)}")
    
    # Calculate potential PCH optimization
    if pch_path.exists():
        pch_includes = extract_includes(pch_path)
        pch_coverage = sum(header_counter.get(h, 0) for h in pch_includes)
        coverage_percentage = (pch_coverage / total_includes * 100) if total_includes > 0 else 0
        print(f"\nCurrent PCH coverage: {pch_coverage} includes ({coverage_percentage:.1f}%)")
        
        # Suggest improvements
        suggested_total = sum(count for header, count in high_frequency_headers[:20])
        new_coverage = pch_coverage + suggested_total
        new_percentage = (new_coverage / total_includes * 100) if total_includes > 0 else 0
        print(f"Potential coverage with top 20 suggestions: {new_coverage} includes ({new_percentage:.1f}%)")

if __name__ == "__main__":
    main()
    