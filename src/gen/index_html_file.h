const uint8_t data_array[] = {
    0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0xc5, 0x57, 0x6d, 0x6f, 0x22, 0x37,
    0x10, 0xfe, 0xce, 0xaf, 0x70, 0x5d, 0x29, 0x80, 0x4a, 0x58, 0xc8, 0x5d, 0xae, 0x39, 0x60, 0x91,
    0x7a, 0x24, 0xaa, 0x22, 0xe5, 0x2e, 0x91, 0x92, 0xea, 0xd4, 0x8f, 0x66, 0x77, 0x00, 0x37, 0x5e,
    0x1b, 0xd9, 0x5e, 0x08, 0xad, 0xee, 0xbf, 0x77, 0x6c, 0xef, 0x2e, 0x7b, 0x81, 0xa0, 0x14, 0xa5,
    0x3a, 0x3e, 0xb0, 0x7e, 0x19, 0xcf, 0x3c, 0xf3, 0xcc, 0xec, 0xec, 0x78, 0xf4, 0xd3, 0xe5, 0xed,
    0xe4, 0xe1, 0xcf, 0xbb, 0x2b, 0xb2, 0xb0, 0x99, 0x18, 0x37, 0x46, 0xe1, 0x41, 0xc8, 0x68, 0x01,
    0x2c, 0x75, 0x03, 0x1c, 0x66, 0x60, 0x19, 0x91, 0x2c, 0x83, 0x98, 0xae, 0x38, 0xac, 0x97, 0x4a,
    0x5b, 0x4a, 0x12, 0x25, 0x2d, 0x48, 0x1b, 0xd3, 0x35, 0x4f, 0xed, 0x22, 0x4e, 0x61, 0xc5, 0x13,
    0x38, 0xf5, 0x93, 0x0e, 0xe1, 0x92, 0x5b, 0xce, 0xc4, 0xa9, 0x49, 0x98, 0x80, 0xb8, 0xdf, 0x21,
    0x19, 0x7b, 0xe2, 0x59, 0x9e, 0x6d, 0x17, 0x72, 0x03, 0xda, 0xcf, 0xd8, 0x14, 0x17, 0xa4, 0xa2,
    0x85, 0x2d, 0xc1, 0xe5, 0x23, 0xd1, 0x20, 0x62, 0x6a, 0xec, 0x46, 0x80, 0x59, 0x00, 0xa0, 0xb1,
    0x85, 0x86, 0x59, 0x4c, 0x23, 0xbf, 0xd4, 0x4d, 0x8c, 0xf1, 0xd2, 0xa3, 0xa8, 0xc4, 0x38, 0x9a,
    0xaa, 0x74, 0x43, 0x12, 0xc1, 0x8c, 0x89, 0xa9, 0x54, 0x06, 0x04, 0x24, 0x78, 0x8a, 0x09, 0x3e,
    0x97, 0x31, 0x4d, 0x10, 0x26, 0x68, 0x4a, 0xfc, 0xe9, 0x98, 0x4e, 0x59, 0xf2, 0x38, 0xd7, 0x2a,
    0x97, 0xe9, 0x69, 0xa2, 0x84, 0xd2, 0x83, 0xf5, 0x82, 0x5b, 0x28, 0xed, 0xa7, 0x7c, 0x45, 0x78,
    0x8a, 0x87, 0x94, 0x94, 0xa8, 0x84, 0x2b, 0x79, 0x6f, 0x99, 0xcd, 0xd1, 0xe2, 0xa4, 0x58, 0x91,
    0xf3, 0x6e, 0xb7, 0x3b, 0x8a, 0x50, 0xb0, 0x38, 0x62, 0x9d, 0x0b, 0xfe, 0x50, 0xc6, 0xb8, 0x7c,
    0x70, 0xb3, 0xca, 0x98, 0xe7, 0x63, 0xf0, 0xbe, 0xd7, 0x5b, 0x3e, 0x0d, 0x33, 0xa6, 0xe7, 0x5c,
    0x0e, 0x58, 0x6e, 0xd5, 0xd0, 0x9f, 0x39, 0x15, 0x6c, 0xa3, 0x72, 0x3b, 0x98, 0xf1, 0x27, 0x48,
    0x29, 0x99, 0x5c, 0xdd, 0xdc, 0xdc, 0xdf, 0xfd, 0x36, 0xb9, 0xfe, 0xf2, 0x7b, 0xdc, 0xef, 0x05,
    0xe5, 0x4e, 0xbd, 0x2e, 0x87, 0x38, 0xe1, 0xd9, 0x3c, 0xc0, 0xc3, 0x68, 0x68, 0x76, 0x9d, 0xb1,
    0xb9, 0xb3, 0xa5, 0x93, 0x98, 0xee, 0x35, 0xb9, 0x00, 0x3e, 0x5f, 0xd8, 0xc1, 0xd9, 0x39, 0x4e,
    0xe8, 0x78, 0x14, 0xd9, 0xb4, 0x52, 0x1b, 0xa1, 0x5e, 0xb2, 0xd7, 0x06, 0x0a, 0xd5, 0x25, 0xfd,
    0x4a, 0x49, 0xee, 0x34, 0xb7, 0x56, 0x49, 0x4a, 0x30, 0xf8, 0x2a, 0x4f, 0x16, 0xc6, 0x32, 0x6d,
    0xe3, 0xa6, 0x01, 0x99, 0x7e, 0xf2, 0x3b, 0xd7, 0x72, 0x99, 0xdb, 0x16, 0xfd, 0xac, 0x56, 0x30,
    0x61, 0x9a, 0x76, 0x68, 0x9f, 0xb6, 0x9b, 0xa5, 0x34, 0x4a, 0x1d, 0x94, 0xed, 0xa1, 0xec, 0x78,
    0x64, 0x96, 0x4c, 0x96, 0xe6, 0x98, 0xd6, 0x6a, 0x6d, 0x28, 0x19, 0x9f, 0xfc, 0x7c, 0xf1, 0xe1,
    0xd7, 0x8f, 0xc3, 0x51, 0xe4, 0x76, 0x77, 0xe1, 0xed, 0xba, 0xf6, 0x82, 0x67, 0x47, 0xfb, 0xf1,
    0xee, 0x0d, 0xfd, 0xb8, 0x78, 0xd1, 0x8f, 0x67, 0xf0, 0x82, 0x80, 0xdb, 0x7b, 0x13, 0x1f, 0xde,
    0xbf, 0x9d, 0x0f, 0x17, 0xbd, 0x7d, 0x3e, 0x1c, 0x62, 0xfe, 0x0d, 0x73, 0xea, 0xec, 0x0d, 0xfd,
    0xe8, 0x1f, 0x95, 0x53, 0xd1, 0xd8, 0xff, 0xbd, 0x94, 0x64, 0xc5, 0x9b, 0x68, 0xe1, 0xc9, 0x9e,
    0xfa, 0x22, 0x34, 0x10, 0x30, 0xb3, 0x18, 0xce, 0xe9, 0xf8, 0x7e, 0x09, 0x90, 0x0e, 0x46, 0xd1,
    0x74, 0x1f, 0x1f, 0x4a, 0x38, 0x28, 0xf1, 0xd9, 0x76, 0x39, 0x14, 0xa4, 0x02, 0xb9, 0x11, 0x3c,
    0x05, 0x57, 0x77, 0xb1, 0xcc, 0x60, 0x41, 0xab, 0x49, 0xf9, 0xd2, 0xe0, 0x5c, 0x27, 0x76, 0xb3,
    0x44, 0xcb, 0x9a, 0x49, 0x57, 0x18, 0x32, 0x8e, 0xe5, 0xaf, 0x47, 0x5d, 0xf5, 0x8d, 0xe9, 0xd9,
    0xf9, 0x39, 0x25, 0x2b, 0x26, 0x72, 0xdc, 0xef, 0x9f, 0xe3, 0x6a, 0x5d, 0x2b, 0x96, 0x47, 0x57,
    0x56, 0x3c, 0x3a, 0x17, 0x09, 0xaf, 0x6c, 0x0f, 0xb1, 0x41, 0xa0, 0xe3, 0xd5, 0x20, 0xb1, 0x35,
    0x04, 0xb5, 0x8a, 0x58, 0x10, 0xf6, 0xac, 0xd6, 0x14, 0xbf, 0x23, 0x38, 0xbb, 0xf1, 0x45, 0xec,
    0xf5, 0x9c, 0xfd, 0x4f, 0xa4, 0xed, 0xa7, 0xcc, 0x83, 0x3b, 0x44, 0x59, 0x10, 0x78, 0x2d, 0x65,
    0x35, 0x86, 0x0e, 0x95, 0xb1, 0x03, 0x6c, 0xdd, 0x31, 0xf9, 0x43, 0xf2, 0xab, 0x7f, 0xd1, 0xab,
    0xa8, 0xfa, 0xb8, 0x9f, 0x2b, 0x84, 0x76, 0x88, 0x29, 0xb7, 0x7d, 0x54, 0x6a, 0x1d, 0xc1, 0xd2,
    0x03, 0x17, 0x3f, 0x28, 0xa5, 0x5e, 0xc1, 0x93, 0x03, 0x77, 0x88, 0x28, 0xbf, 0x7f, 0x64, 0x46,
    0x85, 0x61, 0xa3, 0x98, 0xbb, 0x36, 0xc4, 0xc9, 0x87, 0xb9, 0x49, 0x34, 0x5f, 0xda, 0xd0, 0x4f,
    0x44, 0x13, 0xdf, 0x62, 0x74, 0xff, 0xc2, 0xd6, 0x07, 0x25, 0xc3, 0xd6, 0x78, 0x8f, 0xdc, 0x03,
    0x93, 0x8f, 0x97, 0x9a, 0xaf, 0xe0, 0x80, 0x68, 0x09, 0x69, 0xc5, 0x34, 0x59, 0xc3, 0xf4, 0x5e,
    0x25, 0x8f, 0x60, 0x83, 0x81, 0x3f, 0xb4, 0x20, 0x31, 0xa1, 0x6b, 0x33, 0x88, 0x22, 0x4a, 0x7e,
    0x21, 0x6b, 0x2e, 0x53, 0xb5, 0xee, 0x0a, 0x95, 0x30, 0xd7, 0x7d, 0x75, 0x17, 0xca, 0x58, 0xd7,
    0x7b, 0xe2, 0x56, 0x89, 0x89, 0x0e, 0xf7, 0xab, 0xd3, 0x9e, 0x9e, 0xff, 0xa8, 0x30, 0x1c, 0xa2,
    0xc3, 0x1a, 0x2f, 0x95, 0x62, 0xf3, 0x9d, 0xe2, 0xd2, 0x6a, 0xf1, 0x98, 0xe5, 0xd2, 0xf7, 0x87,
    0x24, 0x85, 0x19, 0x68, 0x0d, 0xe9, 0x35, 0x36, 0xbd, 0xad, 0x36, 0xf9, 0xa7, 0xa2, 0x3f, 0x8a,
    0xc8, 0xbd, 0xfb, 0x98, 0x21, 0x04, 0xbb, 0x20, 0x09, 0xea, 0x0c, 0xb9, 0xb1, 0xed, 0x2d, 0x2b,
    0xd1, 0x1d, 0x63, 0xe8, 0x82, 0x6b, 0xa2, 0xcb, 0xe9, 0xd7, 0xd2, 0xcb, 0x56, 0x7b, 0x58, 0xd7,
    0xff, 0x95, 0x71, 0x4b, 0x66, 0x4a, 0x93, 0x25, 0x76, 0x82, 0xc4, 0x2a, 0x32, 0x05, 0x04, 0x26,
    0xc4, 0x06, 0x4f, 0x63, 0xd3, 0xcb, 0xd0, 0xca, 0x0a, 0x70, 0x11, 0x45, 0x80, 0xf8, 0x2f, 0x2b,
    0xf6, 0xaf, 0x24, 0x74, 0x8f, 0x95, 0x1e, 0x03, 0xf6, 0x81, 0x67, 0x80, 0xad, 0x68, 0x2b, 0xd8,
    0x74, 0xbb, 0x5b, 0x8b, 0xbb, 0x01, 0x6b, 0x77, 0x48, 0xbf, 0xd7, 0xeb, 0x55, 0x50, 0xbe, 0x35,
    0x9e, 0x73, 0xf2, 0x3c, 0x65, 0x1f, 0x61, 0xd3, 0x09, 0x49, 0x5f, 0x27, 0x88, 0xcf, 0x48, 0x6b,
    0xd7, 0xf3, 0x93, 0x93, 0x5d, 0x3a, 0xba, 0x1a, 0x5b, 0xfd, 0x8d, 0x6b, 0xc5, 0x81, 0xc4, 0x71,
    0x4c, 0x2a, 0x74, 0xdd, 0xdb, 0xbb, 0xab, 0x2f, 0x75, 0x9d, 0x21, 0x78, 0x29, 0xc3, 0x2b, 0x4b,
    0x4c, 0xd0, 0xac, 0x0b, 0x73, 0xc7, 0xe5, 0x81, 0xb7, 0x3e, 0xac, 0xc9, 0xed, 0x1a, 0x71, 0xa8,
    0x5b, 0xee, 0x68, 0x8d, 0xe4, 0x6f, 0x8d, 0xfa, 0xb3, 0x51, 0x31, 0x7f, 0xe9, 0xa2, 0xbe, 0xc5,
    0x51, 0x5e, 0x79, 0xf8, 0xdf, 0x3e, 0xcb, 0x48, 0x2e, 0x2d, 0x17, 0x84, 0xcd, 0x30, 0x08, 0x21,
    0x36, 0xdc, 0x14, 0x81, 0x11, 0x8a, 0xa5, 0x90, 0x36, 0xb6, 0x0c, 0xa4, 0x2a, 0xc9, 0x33, 0xbc,
    0xa3, 0x3c, 0xf7, 0xb1, 0xe9, 0x24, 0x31, 0x5c, 0xcd, 0xba, 0x7b, 0x95, 0x30, 0x4b, 0xd3, 0xab,
    0x15, 0x0e, 0x6e, 0xb8, 0xc1, 0x7b, 0x18, 0xe8, 0x56, 0xf3, 0xf2, 0xf6, 0xf3, 0x24, 0x5c, 0xca,
    0x6e, 0xbc, 0x89, 0x66, 0xa7, 0x8a, 0x47, 0xeb, 0x7b, 0x86, 0x6a, 0x01, 0xaf, 0x27, 0x6f, 0x87,
    0x9c, 0xd7, 0x82, 0x8a, 0x2e, 0x6f, 0x03, 0x4c, 0x40, 0x18, 0xa8, 0xe9, 0x78, 0x95, 0x86, 0x2a,
    0x2d, 0x2a, 0xd0, 0x73, 0xb0, 0x57, 0x02, 0xdc, 0xf0, 0xd3, 0xe6, 0x3a, 0x6d, 0xd5, 0x2e, 0x4c,
    0xed, 0x5d, 0x87, 0x68, 0xd9, 0xe0, 0xd1, 0x9a, 0x23, 0xe0, 0x44, 0xda, 0x5b, 0x20, 0x7e, 0xde,
    0x5d, 0x6a, 0xff, 0xc4, 0x98, 0xb0, 0x5c, 0xe0, 0x3b, 0xd2, 0xa8, 0x1c, 0x08, 0x83, 0xa2, 0xdc,
    0x6d, 0x8b, 0x11, 0x56, 0x7e, 0xbc, 0x32, 0x86, 0xd6, 0x1a, 0xef, 0x91, 0xfe, 0xd2, 0xfb, 0x2f,
    0x7b, 0xed, 0xc0, 0xa6, 0x0c, 0x0f, 0x00, 0x00
};
