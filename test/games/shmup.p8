pico-8 cartridge // http://www.pico-8.com
version 35
__lua__
function _init()
	p={x=60,y=90,speed=1.5}
	bullets={}
	enemies={}
	ship_enemies={}
	explosions={}
	create_stars()
	score = 0
	state = 0
	clock = 0
	last_time = 0
	last_wave = 0
end



function _update60()
	if (state==0) update_game()
	if (state==1) update_gameover()
	clock += 1
end

function _draw()
	if (state==0) draw_game()
	if (state==1) draw_gameover()
end

function update_game()
	update_player()
	update_bullets()
	update_stars()
	if (#enemies==0 or (clock-last_wave > 80)) then
		spawn_enemies(ceil(rnd(9)))
	end 
	if (#ship_enemies==0 and (clock-last_time > 400)) then
		spawn_ship_enemies()
	end 
	update_enemies()
	update_ship_enemies()
	update_explosions()
end

function draw_game()
	cls()
		--stars
		for s in all(stars) do
			pset(s.x,s.y,s.col)
			end	
		--ship
	spr(1,p.x,p.y)
	--enemies
	for e in all(enemies) do
	if(clock%2 == 0) then
		spr(4,e.x,e.y)
	else
	 spr(3,e.x,e.y)
		end
	end
	--enemy ships
	for e in all(ship_enemies) do
	if(clock%2 == 1) then
		spr(5,e.x,e.y)
	else
	 spr(6,e.x,e.y)
		end
	end
	
	--explosions
	draw_explosions()
	--bullets
	for b in all(bullets) do
		spr(2,b.x,b.y)
		end
	--score
	print("score:\n"..score,2,2,7)
	--debug
	--print(clock,2,70,10)
	--print(last_time,2,80,10)
	--print(p.x,2,80,10)
end
-->8
--bullets

function shoot()
	new_bullet={
		x=p.x,
		y=p.y,
		speed=4
	}
	add(bullets,new_bullet)
	sfx(0)
end

function update_bullets()
	for b in all(bullets) do
		b.y-=b.speed
		if b.y<-8 then
			del(bullets,b)
		end
	end
end
-->8
--stars

function create_stars()
	stars={}
	for i=1,25 do
		new_star={
			x=rnd(128),
			y=rnd(128),
			col=rnd({1,6,5}),
			speed = 0.5+rnd(1)
		}
		add(stars,new_star)
	end
		for i=1,10 do
		new_star={
			x=rnd(128),
			y=rnd(128),
			col=rnd({15,9,10}),
			speed = 2+rnd(2)
		}
		add(stars,new_star)
	end
end

function update_stars()
	for s in all (stars) do
		s.y += s.speed
		if s.y > 128 then
			s.y = 0
			s.x = rnd(128)	
		end	
	end
end
-->8
--enemies

function spawn_enemies(amount)
gap=(128-8*amount)/(amount+1)
for i=1, amount do
	new_enemy={
		x=gap*i+8*(i-1),
		y=-20,
		life=4
		}
		add(enemies,new_enemy)
		last_wave = clock
	end
end

function spawn_ship_enemies()
	new_enemy={
		x=rnd(128),
		y=0,
		life=6
		}
		add(ship_enemies,new_enemy)
end



function update_enemies()
	for e in all(enemies) do
	
		e.y+=0.6

		if e.y > 128 then
			del(enemies,e)
		end
		--collision
		for b in all(bullets)do
			if collision(e,b) then
				
				create_explosion(b.x+4,b.y+2)
				del(bullets,b)
				e.life -= 1
				if e.life==0 then
					sfx(2)
					del(enemies,e)
					score+= 100
				end
			end
		end	
	end
end

function update_ship_enemies()
	for e in all(ship_enemies) do
	
	if (e.x>p.x)	e.x-=0.8
	if (e.x<p.x) e.x+=0.8

		if e.y > 128 then
			del(ship_enemies,e)
		end
		--collision
		for b in all(bullets)do
			if collision(e,b) then
				
				create_explosion(b.x+4,b.y+2)
				del(bullets,b)
				e.life -= 1
				if e.life==0 then
					sfx(2)
					del(ship_enemies,e)
					score+= 200
					last_time=clock
				end
			end
		end	
	end
end
-->8
--collision
function collision(a,b)
	if a.x>b.x+8 
	or a.y > b.y+8
	or a.x+8<b.x
	or a.y+8<b.y then
		return false
	else 
		return true	
	end
end


-->8
--explosions

function create_explosion(x,y)
	sfx(1)
	add(explosions,{x=x,
																	y=y,
																	timer=0})
	
end

function update_explosions()
	
	for e in all(explosions) do
		e.timer+=1
		if e.timer==13 then
			del(explosions,e)
		end
	end
end

function draw_explosions()
	circ(x,y,rayon, couleur)
	
	for e in all(explosions) do
		circ(e.x,e.y,e.timer/3,
							8+e.timer%3)
	end
end
-->8
--player

function update_player()
	if (btn(➡️) and p.x < 127)	p.x += p.speed
	if (btn(⬅️) and p.x > 0)	p.x -= p.speed
	if (btn(⬆️) and p.y > 0)	p.y -= p.speed
	if (btn(⬇️) and p.y < 127)	p.y += p.speed
	if (btnp(❎)) shoot()
	
	for e in all(enemies) do
		if collision(e,p) then
		state=1
		end
	end
end
-->8
function update_gameover()
	if (btn(🅾️)) _init()
end

function draw_gameover()
cls(2)
rectfill(31,53,105,79,0)
rectfill(28,50,102,76,1)
print("score:",50,50,7)
print(score,50,58,10)
print("🅾️ to restart",33,70,7)
end
__gfx__
000000000060060000a00a0000222200002222000090090000900900000000000000000000000000000000000000000000000000000000000000000000000000
00000000006c760000a00a0000022000000220006095590660955906000000000000000000000000000000000000000000000000000000000000000000000000
00700700d0dccd0d00a00a0002222220022222206655556666555566000000000000000000000000000000000000000000000000000000000000000000000000
00077000dddddddd009009000282282002822820665ac566665ca566000000000000000000000000000000000000000000000000000000000000000000000000
00077000d566665d0090090002000020020220206055550660555506000000000000000000000000000000000000000000000000000000000000000000000000
00700700ddd66ddd000000002020020200200200b05c7508805c750b000000000000000000000000000000000000000000000000000000000000000000000000
00000000d0d66d0d000000000000000002000020005cc500005cc500000000000000000000000000000000000000000000000000000000000000000000000000
0000000000d00d000000000000000000020000200050050000500500000000000000000000000000000000000000000000000000000000000000000000000000
__sfx__
00010000334503745037450324402d440274401e43014420004000040000400004000040000400004000040000400004000040000400004000040000400004000040000400004000040000400004000040000400
000100002361022620206301c6201c6101b6101960000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
00060000236301e64019630146200d610066100961000600006000060000600006000060000600006000060000600006000060000600006000060000600006000060000600006000060000600006000060000600
