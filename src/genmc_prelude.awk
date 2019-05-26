/^ *\/\// { next }
/DEBUGGENMC/ { next }
/#ifdef/,/#endif/ { next }
/^ *ASSERT\(/ { next }
/Dout\(/ { next }
{ sub(/ *\/\/.*/, "") }
