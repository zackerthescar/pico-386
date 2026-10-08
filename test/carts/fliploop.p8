pico-8 cartridge // http://www.pico-8.com
version 41
__lua__
-- end-to-end flip() loop test: no _update/_draw (see fliploop.expect)
x=0
while true do
 cls(2)
 rectfill(x,60,x+7,67,10)
 x+=8
 flip()
end
