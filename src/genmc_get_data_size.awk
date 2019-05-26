@include "genmc_prelude.awk"
@include "genmc_body.awk"

/size_t get_data_size/,/^  }$/ {
  bodysub()
  sub(/ const$/, "")
  sub(/^  /, "")
  print
}
