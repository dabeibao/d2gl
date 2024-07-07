import re

def extract_chinese_characters(file_path):
    chinese_chars = set()
    with open(file_path, 'r', encoding='utf-8') as file:
        content = file.read()
        # Extract all Chinese characters
        chinese_chars.update(re.findall(r'[\u4e00-\u9fff]', content))
    return chinese_chars

def generate_cpp_array(chinese_chars):
    # Convert characters to Unicode code points and sort them
    unicode_values = sorted([ord(char) for char in chinese_chars])
    # Generate C++ array with 16 characters per line
    lines = []
    for i in range(0, len(unicode_values), 16):
        line = ", ".join(f"0x{value:04X}" for value in unicode_values[i:i+16])
        lines.append(line)
    cpp_array = "unsigned short simplified_chinese_chars[] = {\n    " + ",\n    ".join(lines) + "\n};"
    return cpp_array

def main():
    file_path = 'menu.cpp'  # Replace with your file path
    chinese_chars = extract_chinese_characters(file_path)
    cpp_array = generate_cpp_array(chinese_chars)
    with open('chinese.h', "w") as f:
        f.write(cpp_array + "\n")

if __name__ == '__main__':
    main()
