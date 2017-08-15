#!/bin/sh
# Generate a neatroff output device

FP="/path/to/gs/fonts"		# ghostscript fonts directory; should be in GS_FONTPATH
TP="/path/to/font/devutf"	# output device directory
RES="720"			# device resolution
SCR="-Slatn,arab"		# scripts to include
LIGIGN="\(ct\|st\|sp\|Rp\)"	# pattern of ligatures to ignore

test -n "$1" && FP="$1"
test -n "$2" && TP="$2"

# creating DESC
mkdir -p $TP
echo "fonts 10 R I B BI CR HR HI HB S1 S" >$TP/DESC
echo "res $RES" >>$TP/DESC
echo "hor 1" >>$TP/DESC
echo "ver 1" >>$TP/DESC
echo "unitwidth 10" >>$TP/DESC

# afmconv troff_name font_path extra_mktrfn_options
afmconv()
{
	echo $1
	cat $2 | ./mkfn -a -b -r$RES -t "$1" $3 $4 $5 $6 $7 | \
		sed "/^ligatures /s/ $LIGIGN//g" >$TP/$1
}

# ttfconv troff_name font_path extra_mktrfn_options
ttfconv()
{
	echo $1
	cat $2 | ./mkfn -b -l -o -r$RES $SCR -t $1 $3 $4 $5 $6 $7 | \
		sed "/^ligatures /s/ $LIGIGN//g" >$TP/$1
}

# otfconv troff_name font_path extra_mktrfn_options
otfconv()
{
	TTF="/tmp/.neatmkfn.ttf"
	# convert the OTF file to TTF using fontforge
	fontforge -lang=ff -c "Open(\"$2\"); Generate(\"$TTF\");" >/dev/null 2>&1
	ttfconv $1 $TTF $3 $4 $5 $6 $7
	rm $TTF
}

# stdfont troff_name gs_font urw_font extra_mktrfn_options
stdfont()
{
	if test -f $2
	then
		afmconv $1 "$2" $4 $5 $6 $7 $8 $9
	else
		afmconv $1 "$3" $4 $5 $6 $7 $8 $9
	fi
}

# The standard fonts; ghostscriptfonts or urw-core35
stdfont R	$FP/n021003l.afm $FP/NimbusRoman-Regular.afm	-pTimes-Roman
stdfont I	$FP/n021023l.afm $FP/NimbusRoman-Italic.afm	-pTimes-Italic
stdfont B	$FP/n021004l.afm $FP/NimbusRoman-Bold.afm	-pTimes-Bold
stdfont BI	$FP/n021024l.afm $FP/NimbusRoman-BoldItalic.afm	-pTimes-BoldItalic
stdfont S	$FP/s050000l.afm $FP/StandardSymbolsPS.afm	-pSymbol -s
stdfont S1	$FP/n021003l.afm $FP/NimbusRoman-Regular.afm	-pTimes-Roman -s
stdfont AR	$FP/a010013l.afm $FP/URWGothic-Book.afm		-pAvantGarde-Book
stdfont AI	$FP/a010033l.afm $FP/URWGothic-BookOblique.afm	-pAvantGarde-BookOblique
stdfont AB	$FP/a010015l.afm $FP/URWGothic-Demi.afm		-pAvantGarde-Demi
stdfont AX	$FP/a010035l.afm $FP/URWGothic-DemiOblique.afm	-pAvantGarde-DemiOblique
stdfont HR	$FP/n019003l.afm $FP/NimbusSans-Regular.afm	-pHelvetica
stdfont HI	$FP/n019023l.afm $FP/NimbusSans-Oblique.afm	-pHelvetica-Oblique
stdfont HB	$FP/n019004l.afm $FP/NimbusSans-Bold.afm		-pHelvetica-Bold
stdfont HX	$FP/n019024l.afm $FP/NimbusSans-BoldOblique.afm	-pHelvetica-BoldOblique
stdfont Hr	$FP/n019043l.afm $FP/NimbusSansNarrow-Regular.afm	-pHelvetica-Narrow
stdfont Hi	$FP/n019063l.afm $FP/NimbusSansNarrow-Oblique.afm	-pHelvetica-Narrow-Oblique
stdfont Hb	$FP/n019044l.afm $FP/NimbusSansNarrow-Bold.afm	-pHelvetica-Narrow-Bold
stdfont Hx	$FP/n019024l.afm $FP/NimbusSansNarrow-BdOblique.afm	-pHelvetica-Narrow-BoldOblique
stdfont KR	$FP/b018012l.afm $FP/URWBookman-Light.afm		-pBookman-Light
stdfont KI	$FP/b018032l.afm $FP/URWBookman-LightItalic.afm	-pBookman-LightItalic
stdfont KB	$FP/b018015l.afm $FP/URWBookman-Demi.afm		-pBookman-Demi
stdfont KX	$FP/b018035l.afm $FP/URWBookman-DemiItalic.afm	-pBookman-DemiItalic
stdfont NR	$FP/c059013l.afm $FP/C059-Roman.afm		-pNewCenturySchlbk-Roman
stdfont NI	$FP/c059033l.afm $FP/C059-Italic.afm		-pNewCenturySchlbk-Italic
stdfont NB	$FP/c059016l.afm $FP/C059-Bold.afm		-pNewCenturySchlbk-Bold
stdfont NX	$FP/c059036l.afm $FP/C059-BdIta.afm		-pNewCenturySchlbk-BoldItalic
stdfont PA	$FP/p052003l.afm $FP/P052-Roman.afm		-pPalatino-Roman
stdfont PR	$FP/p052003l.afm $FP/P052-Roman.afm		-pPalatino-Roman
stdfont PI	$FP/p052023l.afm $FP/P052-Italic.afm		-pPalatino-Italic
stdfont PB	$FP/p052004l.afm $FP/P052-Bold.afm		-pPalatino-Bold
stdfont PX	$FP/p052024l.afm $FP/P052-BoldItalic.afm		-pPalatino-BoldItalic
stdfont CR	$FP/n022003l.afm $FP/NimbusMonoPS-Regular.afm	-pCourier
stdfont CI	$FP/n022023l.afm $FP/NimbusMonoPS-Italic.afm	-pCourier-Oblique
stdfont CB	$FP/n022004l.afm $FP/NimbusMonoPS-Bold.afm	-pCourier-Bold
stdfont CX	$FP/n022024l.afm $FP/NimbusMonoPS-BoldItalic.afm	-pCourier-BoldOblique
stdfont ZI	$FP/z003034l.afm $FP/Z003-MediumItalic.afm	-pZapfChancery-MediumItalic

# The first argument of afmconv, ttfconv, and otfconv is the troff
# name of the font and their second argument is its path. Any other
# argument is passed to mkfn directly.  The postscript names of the
# fonts are inferred from the fonts themselves  To change that, you
# can specify their names via the -p argument of *conv functions.

find $FP/ -name '*.afm' | while read FN
do
	afmconv `basename $FN .afm` $FN
done

find $FP/ -name '*.ttf' | while read FN
do
	ttfconv `basename $FN .ttf` $FN
done

find $FP/ -name '*.otf' | while read FN
do
	otfconv `basename $FN .otf` $FN
done
