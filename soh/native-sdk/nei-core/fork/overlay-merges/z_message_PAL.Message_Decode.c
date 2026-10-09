/* overlay-merge 720ff10bb731 e9e553811cf6 97c794eb574c */
void Message_Decode(PlayState* play) {
    // SOH [Link-Span] R04: o corpo foi para Message_DecodeChecked; a mudança do fork (ícone de item do NEI) está
    // na cópia dela em overlay-extra/z_message_PAL.Message_DecodeChecked.c.
    Message_DecodeChecked(play);
}
