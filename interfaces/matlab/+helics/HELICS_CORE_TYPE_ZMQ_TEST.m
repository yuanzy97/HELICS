function v = HELICS_CORE_TYPE_ZMQ_TEST()
  persistent vInitialized;
  if isempty(vInitialized)
    vInitialized = helicsMEX(0, 71);
  end
  v = vInitialized;
end
