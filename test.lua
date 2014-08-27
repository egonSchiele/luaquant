q = require "imagequant"
f = io.open("/tmp/test.png", "rb")
str, err = f:read("*all")
print(convert(str))
