q = require "imagequant"
f = io.open("/tmp/test.png", "rb")
str, err = f:read("*all")
print(q.convert(str,10))
