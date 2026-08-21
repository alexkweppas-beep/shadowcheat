import re

with open(r"D:\Downloads\Frame 2085665376.png", "rb") as f:
    img_bytes = f.read()

array_str = "const unsigned char image_bg_data[{}] = {{\n".format(len(img_bytes))
lines = []
for i in range(0, len(img_bytes), 16):
    chunk = img_bytes[i:i+16]
    lines.append(", ".join([f"0x{b:02x}" for b in chunk]))
array_str += ",\n".join(lines) + "\n};\n"

with open(r"d:\антиграв\333333\444\676767\лоадер 2\src\images_data.hpp", "r") as f:
    content = f.read()

# Replace image_bg_data block
new_content = re.sub(
    r'const unsigned char image_bg_data\[\d+\] = \{.*?^\};',
    array_str,
    content,
    flags=re.MULTILINE | re.DOTALL
)

with open(r"d:\антиграв\333333\444\676767\лоадер 2\src\images_data.hpp", "w") as f:
    f.write(new_content)

print("Updated images_data.hpp")
