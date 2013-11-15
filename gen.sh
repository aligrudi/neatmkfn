#!/bin/sh
# Generate a neatroff output device

# ghostscript font directory; it may contain otf and ttf files also
FP="/mnt/file/gs/fonts"
# output device directory
TP="/root/queue/devutf"
# device resolution
RES="720"
# pattern of ligatures to ignore
LIGIGN="\(ct\|st\|sp\|Rp\)"
# pattern of allowed opentype features
OTFFEATS="\(swsh\|tilt\|smcp\|case\)"

# creating DESC
mkdir -p $TP
echo "fonts 10 R I B BI CW H HI HB S1 S" >$TP/DESC
echo "res $RES" >>$TP/DESC
echo "hor 1" >>$TP/DESC
echo "ver 1" >>$TP/DESC
echo "unitwidth 10" >>$TP/DESC

# afmconv troff_name postscript_name font_filename
function afmconv
{
	echo $1
	cat $FP/$3 | ./mktrfn $4 -a -r$RES -t $1 -p $2 >$TP/$1
}

# converting otf and ttf fonts; needs heirloom's otfdump
# otfconv troff_name postscript_name font_path
function otfconv
{
	ODOUT="/tmp/.mktrfn.otfdump"
	otfdump $3 >$ODOUT
	FEATS="`grep "^feature " $ODOUT | cut -f2 -d ' ' | sort | uniq`"
	echo "$1: `echo $FEATS | tr '\n' ' '`"
	cat $ODOUT | grep -v "^feature " | ./mktrfn $4 -r$RES -p $2 | \
		sed "/^ligatures /s/ $LIGIGN//g" >$TP/$1
	for FEAT in `echo $FEATS | tr ' ' '\n' | sed "/$OTFFEATS/!d"`
	do
		echo "  $1.$FEAT"
		(grep "^feature $FEAT" $ODOUT; grep -v "^feature " $ODOUT) | \
			./mktrfn $4 -r$RES -p $2 | \
			sed "/^ligatures /s/ $LIGIGN//g" >$TP/$1.$FEAT
	done
	rm $ODOUT
}

# The standard fonts
afmconv R	Times-Roman		n021003l.afm
afmconv I	Times-Italic		n021023l.afm
afmconv B	Times-Bold		n021004l.afm
afmconv BI	Times-BoldItalic	n021024l.afm
afmconv S	Symbol			s050000l.afm -s
afmconv S1	Times-Roman		n021003l.afm -s
afmconv AR	AvantGarde-Book		a010013l.afm
afmconv AI	AvantGarde-BookOblique	a010033l.afm
afmconv AB	AvantGarde-Demi		a010015l.afm
afmconv AX	AvantGarde-DemiOblique	a010035l.afm
afmconv H	Helvetica		n019043l.afm
afmconv HI	Helvetica-Oblique	n019063l.afm
afmconv HB	Helvetica-Bold		n019044l.afm
afmconv HX	Helvetica-BoldOblique	n019064l.afm
afmconv Hr	Helvetica-Narrow	n019043l.afm
afmconv Hi	Helvetica-Narrow-Oblique	n019063l.afm
afmconv Hb	Helvetica-Narrow-Bold	n019044l.afm
afmconv Hx	Helvetica-Narrow-BoldOblique	n019064l.afm
afmconv KR	Bookman-Light		b018012l.afm
afmconv KI	Bookman-LightItalic	b018032l.afm
afmconv KB	Bookman-Demi		b018015l.afm
afmconv KX	Bookman-DemiItalic	b018035l.afm
afmconv NR	NewCenturySchlbk-Roman	c059013l.afm
afmconv NI	NewCenturySchlbk-Italic	c059033l.afm
afmconv NB	NewCenturySchlbk-Bold	c059016l.afm
afmconv NX	NewCenturySchlbk-BoldItalic	c059036l.afm
afmconv PA	Palatino-Roman		p052003l.afm
afmconv PR	Palatino-Roman		p052003l.afm
afmconv PI	Palatino-Italic		p052023l.afm
afmconv PB	Palatino-Bold		p052004l.afm
afmconv PX	Palatino-BoldItalic	p052024l.afm
afmconv C	Courier			n022003l.afm
afmconv CO	Courier			n022003l.afm
afmconv CW	Courier			n022003l.afm
afmconv CI	Courier-Oblique		n022023l.afm
afmconv CB	Courier-Bold		n022004l.afm
afmconv CX	Courier-BoldOblique	n022024l.afm
afmconv ZI	ZapfChancery-MediumItalic	z003034l.afm
afmconv ZD	ZapfDingbats		d050000l.afm

# For otf and ttf files, we assume the postscript name of the font
# can be obtained by dropping its extension.  Otherwise, remove the
# -p argument of mktrfn in otfconv function.

for f in $FP/*.otf
do
	otfconv "`basename $f .otf`" "`basename $f .otf`" "$f"
done

for f in $FP/*.ttf
do
	otfconv "`basename $f .ttf`" "`basename $f .ttf`" "$f"
done
