function v = helics_property_time_output_delay()
  persistent vInitialized;
  if isempty(vInitialized)
    vInitialized = helicsMEX(0, 68);
  end
  v = vInitialized;
end
