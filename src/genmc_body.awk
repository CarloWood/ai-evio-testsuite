function bodysub() {
  sub(/\[\[[^]]*\]\] */, "")
  sub(/StreamBufProducer::/, "")
  sub(/StreamBufConsumer::/, "")
  sub(/common\(\)\./, "")
  sub(/cur_gptr_ref/, "(*cur_gptr_ref)");
  sub(/char\*& \(\*cur_gptr_ref\)/, "char** cur_gptr_ref")
  $0 = gensub(/([^_])get_area_block_node/, "\\1(*get_area_block_node)", "g");
  sub(/MemoryBlock\*& \(\*get_area_block_node\)/, "MemoryBlock** get_area_block_node")
  gsub(/std::memory_order/, "memory_order")
  gsub(/std::streambuf::/, "")
  gsub(/std::streamsize/, "streamsize")
  sub(/streamsize&/, "streamsize")
  sub(/std::memory_order/, "memory_order")
  gsub(/nullptr/, "NULL")
  gsub(/bool/, "int")
  gsub(/true/, "1")
  gsub(/false/, "0")
  $0 = gensub(/\(\*([a-zA-Z0-9_]*)\)->block_start\(\)/, "MemoryBlock_block_start(*\\1)", "g")
  $0 = gensub(/\(\*([a-zA-Z0-9_]*)\)->get_size\(\)/, "MemoryBlock_get_size(*\\1)", "g")
  $0 = gensub(/([a-zA-Z0-9_]*)\.load\(([^)]*)\)/, "atomic_load_explicit(\\&\\1, \\2)", "g")
  $0 = gensub(/([a-zA-Z0-9_]*)\.store\(([^)]*)\)/, "atomic_store_explicit(\\&\\1, \\2)", "g")
  $0 = gensub(/([a-zA-Z0-9_]*)\.compare_exchange_strong\(([^,]*), ([^)]*)\)/, "atomic_compare_exchange_strong_explicit(\\&\\1, \\&\\2, \\3, memory_order_acquire)", "g")

  # Uncomment to test with only seq_cst.
  #gsub(/memory_order_[a-z_]*/, "memory_order_seq_cst")
}
