"""gui 包：PySide6 界面层。

整个 GUI 只通过 core_bridge 与 C++ 内核交互，界面代码里不出现任何
http/curl/cookie 细节，方便单独改样式或换内核。
"""
