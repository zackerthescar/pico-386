# TODO: garbage collection

hey dummy,

this VM leaks every heap allocation. it's fine for short-lived carts (<10 minutes runtime
for typical alloc rates). for long-running games it'll OOM. you need a garbage collector.

## what allocates

| type     | when                                         |
| -------- | -------------------------------------------- |
| String   | every literal at cart load + every CONCAT    |
| Table    | every NEWTABLE                               |
| Closure  | every CLOSURE                                |
| Upvalue  | every captured local that becomes "closed"   |
| Node[]   | table hash parts grow/shrink during rehash   |

## what NOT to do

- **don't add refcounting now**. would require reworking every SetLocal / SetTable / SetGlobal
  / SetUpval / etc to dec-old + inc-new. doable but invasive. cycles still leak.
- **don't add incremental/generational unless measured**. STW pauses on `_update` boundary
  should be plenty for PICO-8's bounded alloc rate. start simple.

## what to do

stop-the-world mark-sweep, triggered at `_update` returning to the host (or every N bytes
allocated, whichever comes first):

1. add GC header to every heap object:
   ```c
   typedef struct GcObject {
       uint8_t color;            // 0=white, 1=black
       uint8_t type;             // GC_STR, GC_TAB, GC_CLOSURE, GC_UPVAL
       uint16_t _pad;
       struct GcObject* next_alloc;  // intrusive linked list
   } GcObject;
   ```
   String/Table/Closure/Upvalue all start with this header.
2. maintain `vm->all_allocs` linked list. every alloc inserts at head.
3. **roots**:
   - walk `value_stack[0 .. top]`, mark TAG_STR/TAG_TAB/TAG_FUNC values
   - walk `globals[0..256]`, same
   - walk `open_upvalues` linked list, mark each upvalue's value
   - walk `call_stack[0..call_top]`, mark each frame's `closure`
4. **mark phase**: from each root, recursively color reachable objects black.
   - Table: mark `array[0..array_len]`, walk hash nodes' key+val
   - Closure: mark each upvalue
   - Upvalue (closed): mark `value`
   - String: nothing further to mark
5. **sweep phase**: walk `all_allocs`. free white objects, repaint black → white.
   - removing a String also requires removing it from the intern table — use a separate sweep
     pass over the intern table buckets.
6. trigger:
   - end of each `_update` when control returns to host, OR
   - when bytes-allocated-since-last-gc exceeds threshold (start at 64 KB, tune)

complexity: ~300 lines of C. one weekend.

## smoke test that you've done it right

```lua
repeat
  for i=1,10000 do local t = {} end
until false
```

run for an hour in dosbox-x. watch memory via the VM's heap counter. should oscillate
between gc-low-water and gc-high-water, never grow unboundedly.

also test:

```lua
local t = {}
t.self = t  -- cycle
t = nil
-- both the table and the cycle should be collected
```

refcounting would leak this. mark-sweep won't.
