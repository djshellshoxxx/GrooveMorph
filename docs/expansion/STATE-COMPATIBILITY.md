# State and parameter compatibility

Current inspected processor uses JUCE APVTS IDs including masterIntensity, manualTempo, manualMeterNumerator, manualMeterDenominator, phraseBars, dryWet, activeVariation, transitionMode and bypass (verify actual literal constants in PluginProcessor.h before manifest freeze). Existing scene-bank tree and its byte envelope must migrate losslessly.

Migration sequence: validate external byte envelope -> parse without unbounded recursive growth -> schema recognition -> migration of copies -> semantic validation (unique IDs, all finite parameters, limits) -> publish atomic accepted snapshot; on rejection preserve loaded safe prior state/default and surface failure. Never partially apply scene, mapping, gestures or asset references. Future major version is rejected without mutating state. Separate durable canonical scenes from optional bounded undo graph and ephemeral captured audio; define user-visible asset portability options explicitly before F09.

Do not rename existing host parameter IDs even if display labels change. Keep new IDs additive, immutable and tested with existing preset fixtures.