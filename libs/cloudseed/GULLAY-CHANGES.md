# Local changes

DSP/ReverbChannel.h: UpdateLines() kept regenerating the delay-line seeds (a std::vector) on every size, decay or modulation change. The seeds are now cached in a fixed array and regenerated only when the delay seed or the cross-seed changes, so those controls can move on the audio thread without allocating.
- std::sinf/std::sqrtf etc. replaced by the global sinf/sqrtf (not in std:: on every compiler); Parameters.h defines MAX_STR_SIZE and a portable strcpy_s.
