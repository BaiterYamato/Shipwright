/* overlay-merge 151bb412830c 586f190078e4 2e93b1c6bb5b */
void ObjBean_SetupPath(ObjBean* this, PlayState* play) {
    // Link-Span: caminho do fork (estação do feijão) com os pontos em f32 do Unbound. O SeasonBean_Path do fork é
    // C++ (season_scene.cpp) e fica fora da DLL: devolve NULL e o caminho é o da cena.
    Path* path = ObjBean_Path(this, play);
    this->pathPoints = *(Vec3f*)SEGMENTED_TO_VIRTUAL(path->points); // SOH [Unbound]
}