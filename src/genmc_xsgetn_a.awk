@include "genmc_prelude.awk"
@include "genmc_body.awk"

/^std::streamsize StreamBufConsumer::xsgetn_a/,/^}$/ {
  sub(/common\(\)\.gbump/, "streambuf_gbump")
  bodysub()
  sub(/update_get_area\(m_get_area_block_node, cur_gptr, available/, "update_get_area(\\&m_get_area_block_node, \\&cur_gptr, \\&available")
  sub(/std::min/, "min")
  print
}
