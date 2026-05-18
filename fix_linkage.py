import re
with open('Application.cpp', 'r') as f:
    data = f.read()

# remove from current location
data = data.replace('// -----------------------------------------------------------------------\n\nvoid HandleSideMouseButton(int vkCode, bool isDown) {\n  if (!KeybindManager::HandleBind(vkCode, isDown)) {\n    HandleFeatureKeyEvent(vkCode, isDown);\n  }\n}\n', '')

# place it outside anonymous namespace
replacement = '} // anonymous namespace\n\nvoid HandleSideMouseButton(int vkCode, bool isDown) {\n  if (!KeybindManager::HandleBind(vkCode, isDown)) {\n    HandleFeatureKeyEvent(vkCode, isDown);\n  }\n}'
data = data.replace('} // anonymous namespace', replacement)

with open('Application.cpp', 'w') as f:
    f.write(data)
