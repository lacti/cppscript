# cppscript

if you want to use c++ as a script, write a script file like this.

```
#!/user/bin/env cppscript

std::cout << "hello world! << std::endl;
```

## usage

### Hi, there!

```
#!/usr/bin/env cppscript
std::string name;
std::cin >> name;
std::cout << "Hi, " << name << std::endl;
```

### main function

```
#!/usr/bin/env cppscript
int main(int argc, char* argv[]) {
  std::cout << "Hello!" << std::endl;
  return 0;
}
```

### include

```
#!/usr/bin/env cppscript
#include <algorithm>
#include "my_header.h"
if (std::one_of(argc, 1, 2, 3)) my_some();
else my_others(argc, argv);
```

### tsv parser

```
#!/usr/bin/env cppscript
#include "tsv_parser.h"
tsv::parser p(stdin);
while (p.next_line()) {
    std::cout << p.next_field<int>() << std::endl;
    std::cout << p.next_field<const char*>() << std::endl;
}
```
