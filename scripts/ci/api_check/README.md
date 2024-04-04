
```bash

pip install doxmlparser
cd zephyr/doc
rm -Rf _build
make configure
cd _build
ninja doxygen

```

```
f35e9871d508cbe425dce0cbbe61c8f89d157921 -- unicast_start_complete
	unicast_stop_complete
	--- if defined

25173f71cda630d4fb0c860d4e45e6f93c0995dd -- why untouched enums are reported?

562166b685b - wht not reported?

981c79b7ce9 - using CONFIG_*** in header files - warning or notice

```