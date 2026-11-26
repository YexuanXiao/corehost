> 实现状态和未完成目标见 [doc/window/0018-gap-analysis.md](doc/window/0018-gap-analysis.md)。

创建入口点window_entry（参数同client_entry）在corehost/window,window_entry的行为类似defterm和libcorepty的结合体：进入window_entry后，corehost维持控制台并启动控制台窗口，而不是启动伪窗口。该控制台窗口的功能在terminal-window-plan.md定义。window_entry没有vt_in和vt_out，绝不使用任何defterm和libcorepty目录下的代码，其他目录下代码可以任意使用。
添加一个新的二进制目标corehostw，该目标和corehost目标共享main.cpp，但是corehost调用client_entry，而corehostw调用window_entry，使用宏来切换两个目标。
window_entry应该利用libconsole,libcoreio,libinterop,libscreen,libvtparser和libvtstate和common文件夹下的现有代码，实现一个真正的控制台/终端GUI，具有控制台和终端的所有功能。
corehost/window内的代码应该尽可能少，window_entry的目的是组装每个模块，如同defterm和libcorepty。
window_entry不使用conpty，不使用libconpty和libcorepty。corehost一个进程完成所有功能。
使用C++23和现代C++。
使用C++/WinRT操作COM。
你需要像设计和实现C++标准库一样实现corehost的函数和类。
禁止添加可变全局变量，所有变量通过参数传递，禁止添加新的lambda，禁止使用std::mutex（用win32文件夹下的替代品）。禁止使用虚函数（COM除外）。
所有代码要符合现有风格，解决问题应该从真正原因解决，修复BUG不打补丁做特例，代码应该清晰，单一职责。所有测试必须能够自动化执行。
禁止联网搜索，如果实现时有任何困难，可以记录下来然后暂时跳过。
应该先列出计划，并且设置子任务，计划必须和代码无关，计划应该尽可能详细，所有需要考虑的点都应该穷尽。
每完成一个子任务，进行一次提交，然后记录实现状态。
所有设计取舍，需要分文件记录在doc文件夹，计划和任务也要分文件记录在doc文件夹，每完成一个就要记录，并更新相关内容。
如果遇到bug，应该先签出一个独立分支，然后修改代码不断测试直到BUG被修复，然后删除所有因为这次修复bug产生的无效代码，将有用代码合并到主分支。
我的所有要求必须都得到实施，不能自己忽略。有问题停下来问和记录，我也会出错，如果我说的不对会导致代码错误或者存在不合理的要求，必须纠正我。
每修改了相关代码，就应该执行测试，测试通过再进行下一步。
可以使用截图进行实际测试（光标闪烁和动画截图不到可以跳过，由我自己验证）。
代码必须考虑性能，避免同一个状态在多个类中重重复冗余。
代码必须考虑重用缓冲区。
代码必须考虑批处理，而不是一个字符一个字符处理。
代码应该用断言诊断程序BUG，而不是到处if判断。
代码所有需要离开作用域自动释放的句柄必须用win32目录或者cppwinrt的设施。
禁止一切const *char*作为参数列表，使用win32::cstring_view和std::string_view等代替。