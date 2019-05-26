@include "genmc_prelude.awk"
@include "genmc_body.awk"

/size_t unused_in_last_block/ {
  bodysub()
  sub(/ const {/, " {")
  sub(/^  /, "")
  print
}
