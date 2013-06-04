/*
 * =====================================================================================
 *
 *    Description:  Corblivar 2.5D representation wrapper; also encapsulates layout
 *    			generation functionality
 *
 *         Author:  Johann Knechtel, johann.knechtel@ifte.de
 *        Company:  Institute of Electromechanical and Electronic Design, www.ifte.de
 *
 * =====================================================================================
 */

// own Corblivar header
#include "CorblivarDie.hpp"
// required Corblivar headers
#include "Math.hpp"

void CorblivarDie::placeCurrentBlock() {
	vector<Block const*> relevBlocks;

	// current tuple; only mutable block parameters can be edited
	Block const* cur_block = this->getCurrentBlock();

	// sanity check for empty dies
	if (this->getCBL().empty()) {
		this->done = true;
		return;
	}

	// sanity check for previously placed blocks; may occur due to multiple alignment
	// requests in process covering this particular block
	if (cur_block->placed) {
		return;
	}

	// pop relevant blocks from related placement stack
	relevBlocks = this->popRelevantBlocks(this->pi);

	// horizontal placement
	if (this->getDirection(this->pi) == Direction::HORIZONTAL) {

		// first, determine block's y-coordinates
		this->determBlockCoords(this->pi, Coordinate::Y, relevBlocks);
		// second, determine block's x-coordinates (depends on y-coord)
		this->determBlockCoords(this->pi, Coordinate::X, relevBlocks);
	}
	// vertical placement
	else {

		// first, determine block's x-coordinates
		this->determBlockCoords(this->pi, Coordinate::X, relevBlocks);
		// second, determine block's y-coordinates (depends on x-coord)
		this->determBlockCoords(this->pi, Coordinate::Y, relevBlocks);
	}

	// update placement stacks
	this->updatePlacementStacks(this->pi, relevBlocks);

	// mark block as placed
	cur_block->placed = true;

	// placement stacks debugging
	if (CorblivarDie::DBG_STACKS) {
		this->debugStacks();
	}
}

void CorblivarDie::debugStacks() const {
	Block const* cur_block = this->getCurrentBlock();

	cout << "DBG_CORB> ";
	cout << "Processed (placed) CBL tuple " << this->getCBL().tupleString(this->pi) << " on die " << this->id << ": ";
	cout << "LL=(" << cur_block->bb.ll.x << ", " << cur_block->bb.ll.y << "), ";
	cout << "UR=(" << cur_block->bb.ur.x << ", " << cur_block->bb.ur.y << ")" << endl;

	stack<Block const*> tmp_Hi = this->Hi;
	cout << "DBG_CORB> stack Hi: ";
	while (!tmp_Hi.empty()) {
		if (tmp_Hi.size() > 1) {
			cout << tmp_Hi.top()->id << ", ";
		}
		else {
			cout << tmp_Hi.top()->id << endl;
		}
		tmp_Hi.pop();
	}

	stack<Block const*> tmp_Vi = this->Vi;
	cout << "DBG_CORB> stack Vi: ";
	while (!tmp_Vi.empty()) {
		if (tmp_Vi.size() > 1) {
			cout << tmp_Vi.top()->id << ", ";
		}
		else {
			cout << tmp_Vi.top()->id << endl;
		}
		tmp_Vi.pop();
	}
}

bool CorblivarDie::debugLayout() const {
	bool invalid = false;
	bool flag_inner;

	// check blocks against each other for (faulty) overlaps
	for (Block const* a : this->getCBL().S) {

		flag_inner = false;

		for (Block const* b : this->getCBL().S) {

			// ignore in outer loop checked blocks; start inner loop
			// after current block is self-checked
			if (a->id == b->id) {
				flag_inner = true;
				continue;
			}

			// check for block overlaps
			if (flag_inner && Rect::rectsIntersect(a->bb, b->bb)) {
				cout << "DBG_LAYOUT> Invalid layout! die: " << this->id << "; overlapping blocks: " << a->id << ", " << b->id << endl;

				invalid = true;
			}
		}
	}

	return invalid;
}

vector<Block const*> CorblivarDie::popRelevantBlocks(unsigned const& tuple) {
	vector<Block const*> ret;
	unsigned relevBlocksCount;

	// horizontal placement; consider stack Hi
	if (this->getDirection(tuple) == Direction::HORIZONTAL) {

		// relevant blocks count depends on the T-junctions to be covered and the
		// current stack itself
		relevBlocksCount = min(this->getJunctions(tuple) + 1, this->Hi.size());
		ret.reserve(relevBlocksCount);

		// pop relevant blocks into vector for further handling
		while (relevBlocksCount > ret.size()) {
			ret.push_back(move(this->Hi.top()));
			this->Hi.pop();
		}
	}
	// vertical placement; consider stack Vi
	else {

		// relevant blocks count depends on the T-junctions to be covered and the
		// current stack itself
		relevBlocksCount = min(this->getJunctions(tuple) + 1, this->Vi.size());
		ret.reserve(relevBlocksCount);

		// pop relevant blocks into vector for further handling
		while (relevBlocksCount > ret.size()) {
			ret.push_back(move(this->Vi.top()));
			this->Vi.pop();
		}
	}

	return ret;
}

void CorblivarDie::updatePlacementStacks(unsigned const& tuple, vector<Block const*> const& relev_blocks_stack) {
	bool add_to_stack;
	list<Block const*> blocks_add_to_stack;

	// current block
	Block const* cur_block = this->getBlock(tuple);
	// current block's insertion direction
	Direction const cur_dir = this->getDirection(tuple);

	// horizontal placement
	if (cur_dir == Direction::HORIZONTAL) {

		// update vertical stack; add cur_block when no other relevant blocks
		// are to its top side, indepent of overlap in x-direction
		add_to_stack = true;
		for (Block const* b : relev_blocks_stack) {
			if (Rect::rectA_below_rectB(cur_block->bb, b->bb, false)) {
				add_to_stack = false;
				break;
			}
		}
		// actual stack update
		if (add_to_stack) {
			this->Vi.push(cur_block);
		}

		// update horizontal stack; add relevant blocks which have no block to the right,
		// can be simplified by checking against cur_block (only new block which
		// can be possibly right of others)
		for (Block const* b : relev_blocks_stack) {
			if (!Rect::rectA_leftOf_rectB(b->bb, cur_block->bb, true)) {
				// prepending blocks to list retains the (implicit)
				// ordering of blocks popped from stack Hi regarding their
				// insertion order; required for proper stack manipulation
				blocks_add_to_stack.push_front(b);
			}
		}
		// always consider cur_block since it's one of the right-most blocks now
		blocks_add_to_stack.push_front(cur_block);

		// actual stack update
		for (Block const* b : blocks_add_to_stack) {
			this->Hi.push(b);
		}
	}
	// vertical placement
	else {

		// update horizontal stack; add cur_block when no other relevant blocks
		// are to its right side, indepent of overlap in y-direction
		add_to_stack = true;
		for (Block const* b : relev_blocks_stack) {
			if (Rect::rectA_leftOf_rectB(cur_block->bb, b->bb, false)) {
				add_to_stack = false;
				break;
			}
		}
		// actual stack update
		if (add_to_stack) {
			this->Hi.push(cur_block);
		}

		// update vertical stack; add relevant blocks which have no block above,
		// can be simplified by checking against cur_block (only new block which
		// can be possibly above others)
		for (Block const* b : relev_blocks_stack) {
			if (!Rect::rectA_below_rectB(b->bb, cur_block->bb, true)) {
				// prepending blocks to list retains the (implicit)
				// ordering of blocks popped from stack Vi regarding their
				// insertion order; required for proper stack manipulation
				blocks_add_to_stack.push_front(b);
			}
		}
		// always consider cur_block since it's one of the top-most blocks now
		blocks_add_to_stack.push_front(cur_block);

		// actual stack update
		for (Block const* b : blocks_add_to_stack) {
			this->Vi.push(b);
		}
	}
}

void CorblivarDie::determBlockCoords(unsigned const& tuple, Coordinate const& coord, vector<Block const*> const& relev_blocks_stack, bool shifted) const {
	double x, y;
	unsigned b;
	stack<Block const*> Hi, Vi;
	Block const* stack_block;

	// current block
	Block const* cur_block = this->getBlock(tuple);
	// current block's insertion direction
	Direction const cur_dir = this->getDirection(tuple);

	// update x-coordinates
	if (coord == Coordinate::X) {

		// for vertical block insertion; x-coordinate is first coordinate, thus
		// not dependent on y-coordinate
		if (cur_dir == Direction::VERTICAL) {

			// determine x-coordinate for lower left corner of current block
			//
			// all columns are to be covered (according to T-juncts), thus place the
			// block at the left die boundary
			if (this->Vi.empty()) {
				x = 0;
			}
			// only some columns are to be covered, thus determine the left front of
			// the related blocks
			else {
				x = relev_blocks_stack[0]->bb.ll.x;
				for (b = 1; b < relev_blocks_stack.size(); b++) {
					x = min(x, relev_blocks_stack[b]->bb.ll.x);
				}
			}
		}
		// for horizontal insertion; x-coordinate is second coordinate, thus
		// dependent on y-coordinate
		else {

			// determine x-coordinate for lower left corner of current block, consider
			// right front of blocks to be covered
			x = 0;
			for (Block const* b : relev_blocks_stack) {
				// only consider blocks which intersect in y-direction
				if (Rect::rectsIntersectVertical(cur_block->bb, b->bb)) {
					// determine right front
					x = max(x, b->bb.ur.x);
				}
			}

			// additional check for shifted blocks; the x-coordinate of a
			// shifted block may be furthermore dependend on corner blocks in
			// vertical (shifting) direction; thus we also need to check the
			// stack Vi
			if (shifted) {

				// check against all blocks; use a local stack copy
				Vi = this->Vi;

				while (!Vi.empty()) {

					// current block on stack
					stack_block = Vi.top();
					Vi.pop();

					// determine right front, consider only
					// vertically intersecting blocks
					if (Rect::rectsIntersectVertical(cur_block->bb, stack_block->bb)) {
						x = max(x, stack_block->bb.ur.x);
					}
				}
			}
		}

		// update block's x-coordinates
		cur_block->bb.ll.x = x;
		cur_block->bb.ur.x = cur_block->bb.w + x;
	}

	// update y-coordinates
	else {

		// for horizontal block insertion; y-coordinate is first coordinate, thus
		// not dependent on x-coordinate
		if (cur_dir == Direction::HORIZONTAL) {

			// determine y-coordinate for lower left corner of current block
			//
			// all rows are to be covered (according to T-juncts), thus place the
			// block at the bottom die boundary
			if (this->Hi.empty()) {
				y = 0;
			}
			// only some rows are to be covered, thus determine the lower front of
			// the related blocks
			else {
				y = relev_blocks_stack[0]->bb.ll.y;
				for (b = 1; b < relev_blocks_stack.size(); b++) {
					y = min(y, relev_blocks_stack[b]->bb.ll.y);
				}
			}
		}
		// for vertical insertion; y-coordinate is second coordinate, thus
		// dependent on x-coordinate
		else {

			// determine y-coordinate for lower left corner of current block, consider
			// upper front of blocks to be covered
			y = 0;
			for (Block const* b : relev_blocks_stack) {
				// only consider blocks which intersect in x-direction
				if (Rect::rectsIntersectHorizontal(cur_block->bb, b->bb)) {
					// determine upper front
					y = max(y, b->bb.ur.y);
				}
			}

			// additional check for shifted blocks; the y-coordinate of a
			// shifted block may be furthermore dependend on corner blocks in
			// horizontal (shifting) direction; thus we also need to check the
			// stack Hi
			if (shifted) {

				// check against all blocks; use a local stack copy
				Hi = this->Hi;

				while (!Hi.empty()) {

					// current block on stack
					stack_block = Hi.top();
					Hi.pop();

					// determine upper front, consider only
					// horizontally intersecting blocks
					if (Rect::rectsIntersectHorizontal(cur_block->bb, stack_block->bb)) {
						y = max(y, stack_block->bb.ur.y);
					}
				}
			}
		}

		// update block's y-coordinates
		cur_block->bb.ll.y = y;
		cur_block->bb.ur.y = cur_block->bb.h + y;
	}
}

// note that packing may undermine alignment requests; for fixed-offset requests, this is
// more likely to happen than for range-based request
// (TODO) perform packing such that fixed-offset request are enabled/maintained;
// considering that the SA cost optimization covers any alignment mismatch, such
// additional checks for each block / parallel processing of affected dies seems too
// expansive
void CorblivarDie::performPacking(Direction const& dir) {
	list<Block const*> blocks;
	list<Block const*>::iterator i1;
	list<Block const*>::reverse_iterator i2;
	Block const* block;
	Block const* neighbor;
	double x, y;
	double block_front_checked;

	// sanity check for empty dies
	if (this->getCBL().empty()) {
		return;
	}

	// store blocks in separate list, for subsequent sorting
	for (Block const* block : this->getCBL().S) {
		blocks.push_back(move(block));
	}

	if (dir == Direction::HORIZONTAL) {

		// sort blocks by lower-left x-coordinate (ascending order)
		blocks.sort(
			// lambda expression
			[&](Block const* b1, Block const* b2){
				return (b1->bb.ll.x < b2->bb.ll.x)
					// for blocks on same column, sort additionally by
					// their width, putting the bigger back in the
					// list, thus consider them first during
					// subsequent checking for adjacent blocks
					// (reverse list traversal)
					|| ((b1->bb.ll.x == b2->bb.ll.x) && (b1->bb.ur.x < b2->bb.ur.x))
					// for blocks on same column and w/ same width,
					// order additionally by y-coordinate to ease list
					// traversal (relevant blocks are adjacent tuples
					// in list)
					|| ((b1->bb.ll.x == b2->bb.ll.x) && (b1->bb.ur.x == b2->bb.ur.x) && (b1->bb.ll.y < b2->bb.ll.y))
					;
			}
		);

		// for each block, check the adjacent blocks and perform packing by
		// considering the neighbors' nearest right front
		for (i1 = blocks.begin(); i1 != blocks.end(); ++i1) {
			block= *i1;

			// skip blocks at left boundary, they are implicitly packed
			if (block->bb.ll.x == 0.0) {
				continue;
			}

			// init packed coordinate
			x = 0.0;
			// init search stop flag
			block_front_checked = 0.0;

			// check other blocks; walk in reverse order since we only need to
			// consider the blocks to the left; note that, for some reason, we
			// need to start iteration w/ the block itself, otherwise packing
			// results in invalid layouts
			for (i2 = list<Block const*>::reverse_iterator(i1); i2 != blocks.rend(); ++i2) {
				neighbor = *i2;

				if (Rect::rectA_leftOf_rectB(neighbor->bb, block->bb, true)) {

					// determine the packed coordinate by considering
					// the neigbors nearest right front
					x = max(x, neighbor->bb.ur.x);

					// memorize the covered range of the block front
					block_front_checked += Rect::determineIntersection(neighbor->bb, block->bb).h;
				}
				// in case the full block front was checked, we can stop
				// checking other blocks
				if (Math::doubleComp(block->bb.h, block_front_checked)) {
					break;
				}
			}

			// update coordinate on block itself, effects the final layout as well as
			// the currently walked list (which is required for step-wise packing from
			// left to right boundary)
			block->bb.ll.x = x;
			block->bb.ur.x = block->bb.w + x;
		}
	}

	// vertical direction
	else {

		// sort blocks by lower-left y-coordinate (ascending order)
		blocks.sort(
			// lambda expression
			[&](Block const* b1, Block const* b2){
				return (b1->bb.ll.y < b2->bb.ll.y)
					// for blocks on same row, sort additionally by
					// their height, putting the bigger back in the
					// list, thus consider them first during
					// subsequent checking for adjacent blocks
					// (reverse list traversal)
					|| ((b1->bb.ll.y == b2->bb.ll.y) && (b1->bb.ur.y < b2->bb.ur.y))
					// for blocks on same row and w/ same height,
					// order additionally by x-coordinate to ease list
					// traversal (relevant blocks are adjacent tuples
					// in list)
					|| ((b1->bb.ll.y == b2->bb.ll.y) && (b1->bb.ur.y == b2->bb.ur.y) && (b1->bb.ll.x < b2->bb.ll.x))
					;
			}
		);

		// for each block, check the adjacent blocks and perform packing by
		// considering the neighbors' nearest upper front
		for (i1 = blocks.begin(); i1 != blocks.end(); ++i1) {
			block= *i1;

			// skip blocks at bottom boundary, they are implicitly packed
			if (block->bb.ll.y == 0.0) {
				continue;
			}

			// init packed coordinate
			y = 0.0;
			// init search stop flag
			block_front_checked = 0.0;

			// check other blocks; walk in reverse order since we only need to
			// consider the blocks below; note that, for some reason, we need
			// to start iteration w/ the block itself, otherwise packing
			// results in invalid layouts
			for (i2 = list<Block const*>::reverse_iterator(i1); i2 != blocks.rend(); ++i2) {
				neighbor = *i2;

				if (Rect::rectA_below_rectB(neighbor->bb, block->bb, true)) {

					// determine the packed coordinate by considering
					// the neigbors nearest right front
					y = max(y, neighbor->bb.ur.y);

					// memorize the covered range of the block front
					block_front_checked += Rect::determineIntersection(neighbor->bb, block->bb).w;
				}
				// in case the full block front was checked, we can stop
				// checking other blocks
				if (Math::doubleComp(block->bb.w, block_front_checked)) {
					break;
				}
			}

			// update coordinate on block itself, effects the final layout as
			// well as the currently walked list (which is required for
			// step-wise packing from bottom to top boundary)
			block->bb.ll.y = y;
			block->bb.ur.y = block->bb.h + y;
		}
	}
}
