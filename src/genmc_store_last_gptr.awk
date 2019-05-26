@include "genmc_prelude.awk"
@include "genmc_body.awk"

/void store_last_gptr\(char/,/^  }$/ {
  bodysub()
  sub(/^  /, "")
  print
}
