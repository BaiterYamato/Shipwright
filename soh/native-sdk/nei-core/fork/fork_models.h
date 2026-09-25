// Modelo de get-item de cada item do fork NEI. A tabela sai do fork por gen_item_models.py (sync.py) e fica
// em <build>/nei-fork/nei_item_models.c.
#ifndef LINKSPAN_NEI_FORK_MODELS_H
#define LINKSPAN_NEI_FORK_MODELS_H

#define NEI_MODEL_LAYER_OPAQUE 0
#define NEI_MODEL_LAYER_TRANSLUCENT 1

typedef struct NeiItemModel {
    unsigned char item;  // id lógico do fork (ITEM_*)
    const char* path;    // display list no VFS, sem __OTR__
    float scale;
    unsigned char layer; // NEI_MODEL_LAYER_*
} NeiItemModel;

extern const NeiItemModel gNeiItemModels[];
extern const unsigned int gNeiItemModelCount;

#endif
