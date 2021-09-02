'use strict';

const origNewNode = newNode;
const origShowNode = showNode;
const origExpandNode = expandNode;

newNode = (o, po, text, link, childrenData, lastNode) => {
	let node = origNewNode(o, po, text, link, childrenData, lastNode);

	//console.log('HOOKED NEWNODE GOT CALLED');

	if (!node) {
		return node;
	}

	if (node.childrenData) {
		if (node.plus_img.innerHTML == arrowDown) {
			$(node.plus_img).addClass('expanded');
		} else {
			$(node.plus_img).addClass('unexpanded');
		}

		let oldOnclick = node.expandToggle.onclick;
		node.expandToggle.onclick = (...args) => {
			//console.log('HOOKED ONCLICK GOT CALLED');

			if (node.expanded) {
				$(node.plus_img).removeClass('expanded');
				$(node.plus_img).addClass('unexpanded');
			} else {
				$(node.plus_img).removeClass('unexpanded');
				$(node.plus_img).addClass('expanded');
			}

			return oldOnclick(...args);
		};
	} else {
		$(node.itemDiv).find('.arrow').addClass('no-expand');
	}

	return node;
};

showNode = (o, node, index, hash) => {
	let result = origShowNode(o, node, index, hash);

	//console.log('HOOKED SHOWNODE GOT CALLED');

	if (node.expanded) {
		$(node.plus_img).removeClass('unexpanded');
		$(node.plus_img).addClass('expanded');
	} else {
		$(node.plus_img).removeClass('expanded');
		$(node.plus_img).addClass('unexpanded');
	}

	return result;
};

expandNode = (o, node, imm, showRoot) => {
	if (!node.expanded) {
		$(node.plus_img).removeClass('unexpanded');
		$(node.plus_img).addClass('expanded');
	}

	return origExpandNode(o, node, imm, showRoot);
};
