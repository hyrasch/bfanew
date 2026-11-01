# Client analysis scripts (arm64 libgame.so)

Python 3 with `pyelftools` and `capstone`. Every script reads the library at `apk/lib/arm64-v8a/libgame.so`
relative to the current directory, or at `$LIBGAME`. Symbols are mangled names (`_ZN10FriendInfo...`).

- `syms.py <regex>`: list the functions whose mangled name matches.
- `dis.py <mangled prefix>... [--max N]`: disassembly with symbolized branch targets and string references.
- `keymap.py <readParam symbol> <class prefix, e.g. 10FriendInfo>`: follows the strcmp chain of a
  `*Response::readParam` to the setter / field each JSON key feeds (how the wire formats were mapped).
- `getters.py <class prefix>`: the class's accessors with the object offsets they touch.
- `vt.py <_ZTV symbol> <slot offsets>`: vtable slots (relocation aware).
