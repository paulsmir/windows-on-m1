/* RC44 readback contract. Original synthetic data, no captured shader code. */
#ifndef AGX_D3D12_MATERIAL_ORACLE_H
#define AGX_D3D12_MATERIAL_ORACLE_H
static unsigned rc44_bad_pixels(const unsigned char *colour, const unsigned char *index, unsigned which) {
    /* Descriptor 2/layer 1 and descriptor 7/layer 0; instance tags 3 and 5.
     * These literal expectations are independent of the upload generator. */
    static const unsigned char expected[2][2][4] = {
        {{62, 64, 70, 255}, {3, 2, 1, 255}},
        {{150, 106, 78, 255}, {5, 7, 0, 255}}
    };
    unsigned bad = 0;
    if (!colour || !index || which >= 2) return 16;
    for (unsigned y = 0; y < 4; ++y) for (unsigned x = 0; x < 4; ++x) {
        unsigned mismatch = 0, offset = y * 256 + x * 4;
        for (unsigned k = 0; k < 4; ++k)
            mismatch |= colour[offset + k] != expected[which][0][k] || index[offset + k] != expected[which][1][k];
        bad += !!mismatch;
    }
    return bad;
}
#endif
