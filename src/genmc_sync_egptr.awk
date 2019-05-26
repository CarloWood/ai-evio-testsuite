@include "genmc_prelude.awk"
@include "genmc_body.awk"

/void sync_egptr\(char/,/^  }$/ {
  bodysub()
  sub(/^  /, "")
  print
}
