/****************************************************************************
*
*   PetAction.h
*   Pet Actions and Modes
*
*   Written by Tristan Cormier (9/5/2024)
*
***/

#ifndef  _PETACTION_H
#define  _PETACTION_H

enum PET_ACTION {
  COMMAND_STAY    = 0x000000,
  COMMAND_FOLLOW  = 0x000001,
  COMMAND_ATTACK  = 0x000002,
  COMMAND_ABANDON = 0x000003
};

enum PET_MODE : uint8 {
  PET_MODE_PASSIVE    = 0x000000,
  PET_MODE_DEFENSIVE  = 0x000001,
  PET_MODE_AGGRESSIVE = 0x000002
};

#endif //_PETACTION_H
