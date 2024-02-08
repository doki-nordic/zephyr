
```bash

pip install doxmlparser
cd zephyr/doc
rm -Rf _build
make configure
cd _build
ninja doxygen

```