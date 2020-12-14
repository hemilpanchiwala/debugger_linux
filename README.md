
# debugger_linux
  
## Using debugger
- For using debugger, you simply need to run the compiled `debugger` by passing the compiled test file as the first parameter which needs to be debugged. 
- For eg. if you need to debug the `test.cpp` file then just pass its compiled file (like `test`) as shown in the command below. 
```
 ./debugger ./test
```
**Note**: As dwarf library is used in the codebase, you need to compile the `test.cpp` file with following command - `gcc -g test.cpp -o test`.
- If you need to compile the debugger after making updates to the source code (`launch_exec.cpp`), use the following command
```
g++ -gdwarf-2 launch_exec.cpp -o debugger $(pkg-config --cflags --libs libdwarf++)
```

## Features
| Command                        | Feature provided                    |
| :------------------------------------ | :-------------------------- |
| **continue**| Continue execution of the program |  
| **break 0xaddress**| Add breakpoint at particular address | 
| **break < filename >:< line >**| Add breakpoint at particular line of file |
| **break func_name**| Add breakpoint at function |
| **register dump**| Dump registers |
| **register read reg_name** | Read register values by providing register name |
| **register write reg_name value** | Write specified value in the register |
| **memory read addr** | Prints the value at the particular address |
| **memory write addr value** | Writes the specified value at particular address |
| **stepinst** | Steps the current instruction even if there is a breakpoint |
| **step** | Steps in - step over instruction till we reach the new line|
| **next** | Steps over - sets a breakpoint at every line in the current function |
| **finish** | Steps out - sets breakpoint at return address and continue execution from there |
| **symbol sym_name** | Lookups the particular symbol |
| **backtrace** | Prints all the frames till the main function using stack unwinding |
| **variables** | Reads the variables present till the current address |

## References
This debugger is made following the blogpost - Writing a Linux Debugger (https://blog.tartanllama.xyz/writing-a-linux-debugger-setup/).
