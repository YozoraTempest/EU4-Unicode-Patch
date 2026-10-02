"""Expected display names and native country-search cases for the private fixture."""
ENGLISH_NAME = "École Straße Ａ 英格兰𠀀"
CASTILIAN_NAME = "ΕΛΛΑΔΑ МОСКВА 卡斯蒂利亚"
CASES = [
    ("英格兰", ENGLISH_NAME, True), ("𠀀", ENGLISH_NAME, True), ("𐀀", None, True),
    ("ecole", ENGLISH_NAME, True), ("e\u0301cole", ENGLISH_NAME, True),
    ("STRASSE", ENGLISH_NAME, True), ("strase", ENGLISH_NAME, True),
    ("ａ", ENGLISH_NAME, False), ("ελλαδα", CASTILIAN_NAME, True),
    ("москва", CASTILIAN_NAME, True), ("unmatched-query", None, True),
    ("𠀀", ENGLISH_NAME, True),
]
