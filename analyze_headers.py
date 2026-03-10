#!/usr/bin/env python3
"""
分析AstraDB项目中所有源文件的头文件使用情况
用于优化预编译头文件(pch.h)
"""
import os
import re
from collections import Counter
from pathlib import Path

def extract_includes(file_path):
    """从源文件中提取所有#include语句"""
    includes = []
    try:
        with open(file_path, 'r', encoding='utf-8') as f:
            content = f.read()
            # 匹配 #include "..." 和 #include <...>
            matches = re.findall(r'^\s*#include\s+[<"]([^>"]+)[>"]', content, re.MULTILINE)
            includes.extend(matches)
    except Exception as e:
        print(f"Error reading {file_path}: {e}")
    return includes

def analyze_project(root_dir):
    """分析整个项目的头文件使用情况"""
    # 收集所有源文件
    source_files = []
    for pattern in ['**/*.cpp', '**/*.hpp', '**/*.h']:
        source_files.extend(Path(root_dir).rglob(pattern))
    
    print(f"找到 {len(source_files)} 个源文件")
    
    # 统计头文件使用情况
    header_counter = Counter()
    file_headers = {}
    
    for file_path in source_files:
        includes = extract_includes(file_path)
        file_headers[file_path] = includes
        header_counter.update(includes)
    
    return header_counter, file_headers

def check_header_exists(header_path, include_dirs, project_root):
    """检查头文件是否存在"""
    # 系统头文件（<...>）
    if header_path.startswith('<'):
        return True  # 系统头文件假设存在
    
    # 项目头文件（"..."）
    header_path = header_path.strip('"')
    
    # 检查相对于项目根目录的路径
    for inc_dir in include_dirs:
        full_path = Path(inc_dir) / header_path
        if full_path.exists():
            return True
    
    # 检查绝对路径
    full_path = Path(project_root) / header_path
    if full_path.exists():
        return True
    
    return False

def main():
    root_dir = Path(__file__).parent
    print(f"分析项目: {root_dir}")
    print("=" * 60)
    
    # 分析头文件使用
    header_counter, file_headers = analyze_project(root_dir)
    
    print(f"\n总共使用了 {len(header_counter)} 个不同的头文件")
    print("=" * 60)
    
    # 按使用频率排序
    print("\n最常用的20个头文件:")
    print("-" * 60)
    for header, count in header_counter.most_common(20):
        print(f"{count:4d}  {header}")
    
    # 项目头文件统计
    print("\n" + "=" * 60)
    print("项目头文件使用频率:")
    print("-" * 60)
    project_headers = [h for h in header_counter if h.startswith('"')]
    for header, count in sorted(project_headers, key=lambda x: -header_counter[x]):
        print(f"{count:4d}  {header}")
    
    # 检查pch.h中的头文件
    print("\n" + "=" * 60)
    print("检查当前pch.h中的头文件:")
    print("-" * 60)
    
    pch_path = root_dir / "src" / "pch.h"
    if pch_path.exists():
        pch_includes = extract_includes(pch_path)
        print(f"\npch.h包含 {len(pch_includes)} 个头文件:")
        
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
            print("\n✅ 存在的头文件:")
            for h in existing_headers:
                count = header_counter.get(h, 0)
                print(f"   ✓ {h} (被使用{count}次)")
        
        if missing_headers:
            print("\n❌ 不存在的头文件:")
            for h in missing_headers:
                print(f"   ✗ {h}")
    
    # 建议应该包含在pch.h中的高频头文件
    print("\n" + "=" * 60)
    print("建议添加到pch.h的高频头文件 (>10次使用):")
    print("-" * 60)
    
    # 当前pch.h中已经包含的头文件
    if pch_path.exists():
        pch_includes = extract_includes(pch_path)
        pch_includes_set = set(h.strip('"').strip('<').strip('>') for h in pch_includes)
    else:
        pch_includes_set = set()
    
    high_frequency_headers = []
    for header, count in header_counter.items():
        if count >= 10 and header not in pch_includes_set:
            # 排除系统头文件（通常已经在标准库中）
            if not header.startswith('<'):
                high_frequency_headers.append((header, count))
    
    high_frequency_headers.sort(key=lambda x: -x[1])
    for header, count in high_frequency_headers[:30]:
        print(f"{count:4d}  {header}")
    
    # 标准库头文件统计
    print("\n" + "=" * 60)
    print("高频标准库头文件 (>5次使用):")
    print("-" * 60)
    std_headers = {h: count for h, count in header_counter.items() if h.startswith('<')}
    std_headers = [(h.strip('<').strip('>'), count) for h, count in std_headers.items()]
    std_headers.sort(key=lambda x: -x[1])
    
    for header, count in std_headers[:20]:
        print(f"{count:4d}  <{header}>")

if __name__ == "__main__":
    main()