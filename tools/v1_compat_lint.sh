#!/bin/sh
# V1/REV1 compatibility lint.
#
# The pulse inputs (Start/Stop/Strobe, all on GPIOB) have DIFFERENT pins and
# polarity per hardware revision: v1's Start/Stop are inverted (idle high),
# v2's are direct. The inversion lives in exactly one place - the accessors
# in analog_data.h (get_afg*_pulse_inputs / get_afg*_pulse_interrupts).
#
# A direct GPIOB read anywhere else is a latent cross-revision polarity bug:
# it will work on the board it was written against and misbehave on the
# other. This bit us for real (TIM3/TIM7 sustain polls hardcoded v2 pins,
# broken on v1 until v3.2.1). Read through the accessors instead.
#
# Run from the repo root; CI runs it on every push. Exit 1 on violation.

fail=0

hits=$(grep -rn "GPIOB->IDR" src/ --include='*.c' 2>/dev/null)
if [ -n "$hits" ]; then
  echo "LINT: direct GPIOB->IDR read outside the analog_data.h accessors:"
  echo "$hits"
  fail=1
fi

hits=$(grep -rn "GPIO_ReadInputDataBit(GPIOB" src/ 2>/dev/null | grep -v "analog_data.h")
if [ -n "$hits" ]; then
  echo "LINT: GPIO_ReadInputDataBit on GPIOB outside analog_data.h:"
  echo "$hits"
  fail=1
fi

hits=$(grep -rn "EXTI_GetFlagStatus" src/*.c 2>/dev/null)
if [ -n "$hits" ]; then
  echo "LINT: EXTI pulse flag read outside the analog_data.h accessors:"
  echo "$hits"
  fail=1
fi

if [ "$fail" -eq 0 ]; then
  echo "v1-compat lint: OK"
fi
exit $fail
