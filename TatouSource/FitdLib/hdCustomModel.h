///////////////////////////////////////////////////////////////////////////////
// TatouHd custom models (AITD Randomizer HD pack)
//
// models_custom/<HQR>_<NNN>.bin replaces model NNN of <HQR> (LISTBODY / LISTBOD2) on screen
// while HD backgrounds are on. Only drawing changes: collisions, scripts, saves and the
// animation state keep using the game's model, and the custom model is posed from it every
// draw (same skeleton, checked on load). Its pictures are models_custom/body_<HQR>_<NNN>.png
// (and other_/ramp_/spheres_), laid out from its own shape.
///////////////////////////////////////////////////////////////////////////////
#pragma once

#ifdef AITD_HD_REPAINT_BUILD
#include <string>

struct sBody;

// Binds the model atlas for this draw (setCurrentBodyNum) and returns the model to draw:
// the custom model, posed like gameBody, when HD is on and models_custom has a valid one;
// otherwise gameBody.
sBody* hdSetCurrentBody(int bodyNum, sBody* gameBody, const std::string& hqrName);

// Reads and checks models_custom/<hqrName>_<NNN>.bin without the game's model.
// Returns an empty string when the file is valid, else the reason (or "no file").
std::string hdCheckCustomModelFile(const char* hqrName, int bodyNum, int* vertices, int* primitives, int* groups);
#endif
