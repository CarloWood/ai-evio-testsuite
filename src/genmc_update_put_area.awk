@include "genmc_prelude.awk"
@include "genmc_body.awk"

/StreamBufProducer::update_put_area/,/^}$/ {
  bodysub()
  sub(/available/, "*available")
  print
}
