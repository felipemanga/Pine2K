//!MENU-ENTRY:Convert Resource Packs
//!MENU-SHORTCUT:C-M-p

// This script looks in the "resources" folder and converts all of the folders into resource packs
// Press Ctrl+Enter to run this script or use the menu.

const transparentIndex = 0;

let palette;

function hash(str){
    str = '"' + str;
    let v = 5381;
    for(let i=0; i<str.length; ++i){
        v = ((v*31 >>> 0) + str.charCodeAt(i)) >>> 0;
    }
    return v;
}

APP.getPalette(pal=>{
    palette = pal;
    if(pal){
        (dir("pine-2k") || [])
            .forEach(project => (dir(`pine-2k/${project}`) || [])
                     .forEach(pack => start(`pine-2k/${project}/${pack}`, pack)));
    }
});

function start(rootPath, root){
    let promises = [];
    let hashes = {};
    let acc = [];
    let count = 0;

    Promise.all((dir(rootPath)||[])
    .filter( file=>/\.png$/i.test(file) )
    .map( (file) => {
        return readImage(`${rootPath}/${file}`)
            .then( image => {
                let img = convert(image, 0, 0, image.width, image.height);
                let key = hash(file.replace(/\..*/g, ""));
                if(hashes[key]){
                    log("Collision: ", key, file, hashes[key].file);
                }
                hashes[key] = {key, file, img, pos:0};
                count++;
                return true;
            });
    })).then(_=>{
        if(count == 0)
            return;

        let acc = [
            (count >> 0)&0xFF,
            (count >> 8)&0xFF,
            (count >> 16)&0xFF,
            (count >> 24)&0xFF
        ];

        Object.values(hashes)
            .forEach( entry => {
                optimize(entry);
            });

        let byHash = Object.values(hashes).sort((a, b) => a.key - b.key);
        let bySize = Object.values(hashes).sort((a, b) => a.img.length - b.img.length);
        let pos = byHash.length * 8 + 4;
        for(let i=0; i<count; ++i){
            bySize[i].pos = pos;
            pos += bySize[i].img.length + 4;
        }

        for(let i=0; i<count; ++i){
            let hash = byHash[i].key;
            let pos = byHash[i].pos;
            acc.push(
                (hash >> 0)&0xFF,
                (hash >> 8)&0xFF,
                (hash >> 16)&0xFF,
                (hash >> 24)&0xFF
            );
            acc.push(
                (pos >> 0)&0xFF,
                (pos >> 8)&0xFF,
                (pos >> 16)&0xFF,
                (pos >> 24)&0xFF
            );
        }

        for(let i=0; i<count; ++i){
            let img = bySize[i].img;
            acc = [
                ...acc,
                (img.length >> 0)&0xFF,
                (img.length >> 8)&0xFF,
                (img.length >> 16)&0xFF,
                (img.length >> 24)&0xFF,
                ...img
            ];
        }

        write(`${rootPath}.res`, new Uint8Array(acc));
        log(`${root} packaged!`);
    })
    .catch(ex=>{
        log(ex);
    });
}

function optimize(entry){
    let minC = 300;
    let maxC = -1;
    let data = entry.img;
    for( let i=2; i<data.length; ++i ){
        let c = data[i];
        if( c == 0 ) continue;
        if( c < minC ) minC = c;
        if( c > maxC ) maxC = c;
    }
    let range = maxC - minC;
    if(range == 0){
        optimize1(minC, entry);
    }else if( range < 16 ){
        optimize16(minC, entry);
    } else {
        optimize256(entry);
    }
}

function optimize256(entry){
    entry.img = [0, 8, ... entry.img];
}

function optimize16(minC, entry){
    let out = [];
    let inp = entry.img;
    minC--;
    for(let i=0; i<inp.length - 2; ++i){
        let c = inp[i + 2];
        if(c) c -= minC;
        if(i&1) out[i>>1] |= c;
        else out[i>>1] = c << 4;
    }
    entry.img = [minC, 4, inp[0], inp[1], ...out];
}

function optimize1(minC, entry){
    let out = [];
    let inp = entry.img;
    let w = inp[0];
    // if(w & 7) w += 8 - (w&7);
    for(let i=0; i<(w >> 3) * inp[1]; ++i)
        out[i] = 0;

    let i = 2;
    for(let y=0; y<inp[1]; ++y){
        for(let x=0; x<inp[0]; ++x){
            let c = inp[i];
            let bit = 7 - (x & 0x7);
            if(c)
                out[(y * w >> 3) + (x >> 3)] |= 1 << bit;
            i++;
        }
    }
    entry.img = [minC, 1, w, inp[1], ...out];
}

function convert(img, xb, yb, xe, ye){
    let bpp = 8;
    let len, bytes, data = img.data;
    let ppb = 8 / bpp;
    let out = [xe - xb, ye - yb];
    let run = [], max = Math.min(palette.length, 1<<bpp);
    let PC = undefined, PCC = 0;

    for( let y=yb; y<ye; ++y ){
        run.length = 0;

        for( let x=xb; x<xe; ++x ){
            let i = (y * img.width + x) * 4;
            let closest = 0;
            let closestDist = Number.POSITIVE_INFINITY;
            let R = data[i++]|0;
            let G = data[i++]|0;
            let B = data[i++]|0;
            let C = (R<<16) + (G<<8) + B;
            let A = data[i++]|0;

            if (A < 128) {
                closest = transparentIndex;
            } else if(C === PC){
                closest = PCC;
            } else {

                for( let c=0; c<max; ++c ){
                    if( c == transparentIndex )
                        continue;
                    const ca = palette[c];
                    const PR = ca[0]|0;
                    const PG = ca[1]|0;
                    const PB = ca[2]|0;
        	        const dist = (R-PR)*(R-PR)
                        + (G-PG)*(G-PG)
                        + (B-PB)*(B-PB)
                    ;

                    if( dist < closestDist ){
                        closest = c;
                        closestDist = dist;
                    }
                }

                PC = C;
                PCC = closest;
            }

            let lx = x - xb;
            let shift = (ppb - 1 - lx%ppb) * bpp;
            run[(lx/ppb)|0] = (run[(lx/ppb)|0]||0) + (closest<<shift);
        }

        out.push(...run);
    }

    return out;
}
