pico-8 cartridge // http://www.pico-8.com
version 41
__lua__
-- end-to-end shapes / print test scene (see shapes.expect)
function _draw()
 cls(1)
 rectfill(2,2,9,9,8)                  -- filled square
 rect(12,2,19,9,11)                   -- outline
 line(22,2,29,9,12)                   -- diagonal
 circfill(36,6,3,9)
 circ(46,6,3,10)
 ovalfill(52,2,61,7,14)
 fillp(0b0101101001011010)
 rectfill(64,2,71,9,0x7c)             -- pattern: 1 bits white, 0 bits blue
 fillp()
 print("h",2,20,7)                    -- text at x,y
 cursor(2,30) print("a",9)            -- text at the cursor, color 9
 print("b")                           -- next line: y=36, pen still 9
 camera(-100,-40) print("c",0,0,10) camera()
 line(80,20,90,20,8) line(90,30)      -- 2-arg form continues from 90,20
 pal(11,3) rectfill(100,2,103,5,11) pal()  -- palette maps 11 -> 3
end
