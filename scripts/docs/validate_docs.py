import os
import re
import sys

def main():
    has_errors = False
    
    # Allow passing a target staging directory, e.g. artifacts/staging
    target_dir = sys.argv[1] if len(sys.argv) > 1 else "."
    book_src = os.path.join(target_dir, "book/src")
    
    summary_path = os.path.join(book_src, "SUMMARY.md")
    
    if not os.path.exists(summary_path):
        print("Error: SUMMARY.md not found.")
        sys.exit(1)
        
    with open(summary_path, 'r', encoding='utf-8') as f:
        summary_content = f.read()
        
    referenced_files = re.findall(r'\]\((.*?\.md)\)', summary_content)
    
    # Check if referenced files exist
    for md_file in referenced_files:
        path = os.path.join(book_src, md_file)
        if not os.path.exists(path):
            print(f"Error: Referenced file does not exist: {path}")
            has_errors = True

    # Check if all .md files in book/src are in SUMMARY.md
    for root, _, files in os.walk(book_src):
        for file in files:
            if file.endswith('.md') and file != 'SUMMARY.md':
                rel_path = os.path.relpath(os.path.join(root, file), book_src)
                rel_path = rel_path.replace("\\", "/")
                if rel_path not in referenced_files:
                    print(f"Error: File {rel_path} is not in SUMMARY.md")
                    has_errors = True
                    
    marketing_words = ["powerful", "robust", "fast", "cutting-edge"]
    
    for md_file in referenced_files:
        path = os.path.join(book_src, md_file)
        if os.path.exists(path):
            with open(path, 'r', encoding='utf-8') as f:
                content = f.read()
                
            code_blocks = content.count('```')
            if code_blocks % 2 != 0:
                print(f"Error: Unclosed fenced code block in {path}")
                has_errors = True
                
            if re.search(r'[\u202A-\u202E\u2066-\u2069]', content):
                print(f"Error: BiDi control characters found in {path}")
                has_errors = True
                
            for word in marketing_words:
                if word in content.lower():
                    print(f"Warning: Low-quality marketing word '{word}' found in {path}")
                    
    changelog_path = os.path.join(book_src, "99-changelog.md")
    if os.path.exists(changelog_path):
        with open(changelog_path, 'r', encoding='utf-8') as f:
            content = f.read().strip()
            if not content:
                print("Error: Changelog is empty.")
                has_errors = True
                
    if has_errors:
        sys.exit(1)
    else:
        print("Validation passed.")
        sys.exit(0)

if __name__ == "__main__":
    main()
