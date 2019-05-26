@include "genmc_prelude.awk"
@include "genmc_body.awk"

/StreamBufConsumer::update_get_area/,/^}$/ {
  bodysub()
  print
}
